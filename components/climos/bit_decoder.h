#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace climos {

// Dekodiert Pegelwechsel der RS485-Leitung zu Zeichen mit 9 Datenbits:
// Start, 8 Daten (LSB zuerst), 9. Bit, Stopp = 11 Bit. Der UART sieht das 9. Bit nicht.
// Nur für die Diagnose. Frames werden per CRC geprüft, Fehlmessungen fallen dadurch heraus.
class BitDecoder {
 public:
  struct Stat {
    uint32_t count{0};
    int32_t min{0};
    int32_t max{0};
    int64_t sum{0};
    void add(int32_t value);
  };

  // Zeiten in Mikrosekunden, Überlauf des 32-Bit-Zählers ist erlaubt
  void add_edge(uint32_t time_us, bool level);
  // now_us muss vor dem Einspeisen der letzten Pegelwechsel gelesen worden sein
  void process(uint32_t now_us);

  uint32_t frames_ok() const { return frames_ok_; }
  uint32_t crc_errors() const { return crc_errors_; }
  uint32_t stop_errors() const { return stop_errors_; }
  // Erstes Zeichen eines Frames bzw. alle weiteren, jeweils mit und ohne 9. Bit
  uint32_t first_marked() const { return first_marked_; }
  uint32_t first_unmarked() const { return first_unmarked_; }
  uint32_t other_marked() const { return other_marked_; }
  uint32_t other_unmarked() const { return other_unmarked_; }
  const Stat &gap_after_poll() const { return gap_after_poll_; }
  const Stat &gap_after_confirm() const { return gap_after_confirm_; }
  const Stat &gap_after_broadcast() const { return gap_after_broadcast_; }
  const Stat &gap_other() const { return gap_other_; }
  const Stat &gap_inside_frame() const { return gap_inside_frame_; }
  // Muster "cmd XX, 9. Bit an Position ..." mit Anzahl und erstem Beispiel-Frame
  struct Pattern {
    uint32_t count{0};
    std::string example;
  };
  const std::map<std::string, Pattern> &patterns() const { return patterns_; }

 protected:
  struct Edge {
    uint32_t time;
    bool level;
  };
  struct Char {
    uint8_t value;
    bool ninth;
    uint32_t start;
  };

  void add_char_(const Char &c);
  void parse_frames_();
  void handle_frame_(size_t size);

  std::vector<Edge> edges_;
  size_t edge_pos_{0};
  std::vector<Char> chars_;
  bool has_last_char_{false};
  uint32_t last_char_start_{0};
  bool has_last_frame_{false};
  uint32_t last_frame_end_{0};
  uint8_t last_frame_cmd_{0};
  uint8_t last_frame_dst_{0};
  uint32_t frames_ok_{0};
  uint32_t crc_errors_{0};
  uint32_t stop_errors_{0};
  uint32_t first_marked_{0};
  uint32_t first_unmarked_{0};
  uint32_t other_marked_{0};
  uint32_t other_unmarked_{0};
  Stat gap_after_poll_;
  Stat gap_after_confirm_;
  Stat gap_after_broadcast_;
  Stat gap_other_;
  Stat gap_inside_frame_;
  std::map<std::string, Pattern> patterns_;
};

}  // namespace climos
}  // namespace esphome
