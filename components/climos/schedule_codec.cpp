#include "schedule_codec.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace climos {

static std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    b--;
  return s.substr(a, b - a);
}

static std::string lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool parse_schedule_text(const std::string &text, uint8_t levels[SCHEDULE_SLOTS], std::string &error) {
  int last_slot = -1;
  int last_level = -1;
  size_t pos = 0;
  int entries = 0;
  while (pos <= text.size()) {
    size_t comma = text.find(',', pos);
    if (comma == std::string::npos)
      comma = text.size();
    const std::string entry = trim(text.substr(pos, comma - pos));
    pos = comma + 1;
    if (entry.empty()) {
      if (comma == text.size())
        break;
      error = "leerer Eintrag";
      return false;
    }
    entries++;

    unsigned int hh = 0, mm = 0;
    int consumed = 0;
    if (std::sscanf(entry.c_str(), "%2u:%2u%n", &hh, &mm, &consumed) != 2 || consumed != 5) {
      error = "Zeit nicht lesbar in \"" + entry + "\" (erwartet HH:MM)";
      return false;
    }
    if (hh > 23 || mm % 15 != 0 || mm > 45) {
      error = "Zeit " + entry.substr(0, 5) + " ungültig (nur Viertelstunden)";
      return false;
    }
    const int slot = static_cast<int>(hh * 4 + mm / 15);
    if (entries == 1 && slot != 0) {
      error = "erster Eintrag muss 00:00 sein";
      return false;
    }
    if (slot <= last_slot) {
      error = "Zeiten nicht aufsteigend bei " + entry.substr(0, 5);
      return false;
    }

    const std::string state = lower(trim(entry.substr(5)));
    int level;
    if (state == "aus") {
      level = 0;
    } else if (state.size() == 7 && state.compare(0, 6, "stufe ") == 0 && state[6] >= '1' && state[6] <= '3') {
      level = state[6] - '0';
    } else {
      error = "Stufe nicht lesbar in \"" + entry + "\" (erwartet aus, Stufe 1..3)";
      return false;
    }

    // Vorherigen Abschnitt bis hierher auffüllen
    for (int s = last_slot + 1; s < slot; s++)
      levels[s] = static_cast<uint8_t>(last_level);
    levels[slot] = static_cast<uint8_t>(level);
    last_slot = slot;
    last_level = level;
  }
  if (entries == 0) {
    error = "kein Eintrag";
    return false;
  }
  for (int s = last_slot + 1; s < SCHEDULE_SLOTS; s++)
    levels[s] = static_cast<uint8_t>(last_level);
  return true;
}

void encode_schedule_day(const uint8_t levels[SCHEDULE_SLOTS], uint8_t regs[3][8]) {
  std::memset(regs, 0, 3 * 8);
  for (uint8_t slot = 0; slot < SCHEDULE_SLOTS; slot++)
    regs[slot / 32][(slot % 32) / 4] |= static_cast<uint8_t>((levels[slot] & 0x03) << ((slot % 4) * 2));
}

std::vector<std::vector<uint8_t>> build_schedule_payloads(uint8_t day_mask, const uint8_t levels[SCHEDULE_SLOTS]) {
  uint8_t regs[3][8];
  encode_schedule_day(levels, regs);

  std::vector<std::vector<uint8_t>> payloads;
  std::vector<uint8_t> current;
  uint8_t in_frame = 0;
  for (uint8_t day = 0; day < 7; day++) {
    if (!(day_mask & (1u << day)))
      continue;
    for (uint8_t r = 0; r < 3; r++) {
      const uint16_t id = SCHEDULE_FIRST_REGISTER + day * 3 + r;
      current.push_back(id & 0xFF);
      current.push_back(id >> 8);
      current.insert(current.end(), regs[r], regs[r] + 8);
      if (++in_frame == SCHEDULE_REGISTERS_PER_FRAME) {
        payloads.push_back(current);
        current.clear();
        in_frame = 0;
      }
    }
  }
  if (!current.empty())
    payloads.push_back(current);
  return payloads;
}

}  // namespace climos
}  // namespace esphome
