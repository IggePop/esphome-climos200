#include "bit_decoder.h"

#include <algorithm>
#include <cstdio>

namespace esphome {
namespace climos {

static const float BIT_US = 1000000.0f / 9600.0f;
static const uint8_t BITS_PER_CHAR = 11;
static const uint8_t HEADER_SIZE_BYTES = 6;
static const uint8_t CMD_POLL = 0x84;
static const uint8_t CMD_CONFIRM = 0x86;
static const uint8_t BROADCAST = 0xFF;
// Längere Pause beendet ein unvollständiges Frame
static const int32_t FRAME_TIMEOUT_US = 20000;
static const size_t MAX_PATTERNS = 32;
static const size_t MAX_EXAMPLE_BYTES = 16;

static int32_t elapsed(uint32_t from, uint32_t to) { return static_cast<int32_t>(to - from); }

static uint16_t crc16_xmodem(uint16_t crc, uint8_t byte) {
  crc ^= static_cast<uint16_t>(byte) << 8;
  for (uint8_t bit = 0; bit < 8; bit++) {
    crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void BitDecoder::Stat::add(int32_t value) {
  this->min = this->count == 0 ? value : std::min(this->min, value);
  this->max = this->count == 0 ? value : std::max(this->max, value);
  this->sum += value;
  this->count++;
}

void BitDecoder::add_edge(uint32_t time_us, bool level) { this->edges_.push_back({time_us, level}); }

void BitDecoder::process(uint32_t now_us) {
  while (true) {
    while (this->edge_pos_ < this->edges_.size() && this->edges_[this->edge_pos_].level) {
      this->edge_pos_++;
    }
    if (this->edge_pos_ >= this->edges_.size())
      break;

    const uint32_t start = this->edges_[this->edge_pos_].time;
    // Stoppbit muss vorbei sein, nach ihm kommt nicht zwingend ein Pegelwechsel
    if (elapsed(start, now_us) < static_cast<int32_t>(BITS_PER_CHAR * BIT_US))
      break;

    // Jedes Bit in seiner Mitte abtasten: Position 0 Start, 1..8 Daten, 9 = 9. Bit, 10 Stopp
    size_t edge = this->edge_pos_;
    uint16_t bits = 0;
    for (uint8_t i = 0; i < BITS_PER_CHAR; i++) {
      const uint32_t sample = start + static_cast<uint32_t>((i + 0.5f) * BIT_US);
      while (edge + 1 < this->edges_.size() && elapsed(this->edges_[edge + 1].time, sample) >= 0) {
        edge++;
      }
      if (this->edges_[edge].level)
        bits |= 1u << i;
    }

    if (bits & 0x01) {
      // Störimpuls, kein Startbit
      this->edge_pos_++;
      continue;
    }
    if (!(bits & (1u << 10)))
      this->stop_errors_++;
    this->add_char_({static_cast<uint8_t>(bits >> 1), (bits & (1u << 9)) != 0, start});
    this->edge_pos_ = edge + 1;
  }

  if (this->edge_pos_ > 0) {
    this->edges_.erase(this->edges_.begin(), this->edges_.begin() + this->edge_pos_);
    this->edge_pos_ = 0;
  }
}

void BitDecoder::add_char_(const Char &c) {
  if (this->has_last_char_ && !this->chars_.empty() && elapsed(this->last_char_start_, c.start) > FRAME_TIMEOUT_US) {
    this->crc_errors_++;
    this->chars_.clear();
  }
  this->has_last_char_ = true;
  this->last_char_start_ = c.start;
  this->chars_.push_back(c);
  this->parse_frames_();
}

void BitDecoder::parse_frames_() {
  while (this->chars_.size() >= 4) {
    const size_t size = HEADER_SIZE_BYTES + (this->chars_[3].value & 0x7F);
    if (this->chars_.size() < size)
      return;

    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++) {
      if (i != 4 && i != 5)
        crc = crc16_xmodem(crc, this->chars_[i].value);
    }
    const uint16_t expected = this->chars_[4].value | (this->chars_[5].value << 8);
    if (crc != expected) {
      this->crc_errors_++;
      this->chars_.erase(this->chars_.begin());
      continue;
    }
    this->handle_frame_(size);
    this->chars_.erase(this->chars_.begin(), this->chars_.begin() + size);
  }
}

void BitDecoder::handle_frame_(size_t size) {
  const uint8_t dst = this->chars_[1].value;
  const uint8_t cmd = this->chars_[2].value;
  this->frames_ok_++;

  std::string marks;
  char buf[16];
  for (size_t i = 0; i < size; i++) {
    const Char &c = this->chars_[i];
    if (i == 0) {
      (c.ninth ? this->first_marked_ : this->first_unmarked_)++;
    } else {
      (c.ninth ? this->other_marked_ : this->other_unmarked_)++;
      this->gap_inside_frame_.add(elapsed(this->chars_[i - 1].start, c.start) -
                                  static_cast<int32_t>(BITS_PER_CHAR * BIT_US));
    }
    if (c.ninth) {
      std::snprintf(buf, sizeof(buf), "%s%u", marks.empty() ? "" : ",", static_cast<unsigned int>(i));
      marks += buf;
    }
  }

  if (this->has_last_frame_) {
    const int32_t gap = elapsed(this->last_frame_end_, this->chars_[0].start);
    if (this->last_frame_cmd_ == CMD_POLL) {
      (this->last_frame_dst_ == BROADCAST ? this->gap_after_broadcast_ : this->gap_after_poll_).add(gap);
    } else if (this->last_frame_cmd_ == CMD_CONFIRM) {
      this->gap_after_confirm_.add(gap);
    } else {
      this->gap_other_.add(gap);
    }
  }
  this->has_last_frame_ = true;
  this->last_frame_end_ = this->chars_[size - 1].start + static_cast<uint32_t>(BITS_PER_CHAR * BIT_US);
  this->last_frame_cmd_ = cmd;
  this->last_frame_dst_ = dst;

  std::snprintf(buf, sizeof(buf), "cmd %02X", cmd);
  const std::string key = std::string(buf) + ", 9. Bit an Position: " + (marks.empty() ? "keine" : marks);
  auto it = this->patterns_.find(key);
  if (it != this->patterns_.end()) {
    it->second.count++;
  } else if (this->patterns_.size() < MAX_PATTERNS) {
    Pattern pattern;
    pattern.count = 1;
    for (size_t i = 0; i < size && i < MAX_EXAMPLE_BYTES; i++) {
      std::snprintf(buf, sizeof(buf), "%02X%s ", this->chars_[i].value, this->chars_[i].ninth ? "*" : "");
      pattern.example += buf;
    }
    this->patterns_[key] = pattern;
  }
}

}  // namespace climos
}  // namespace esphome
