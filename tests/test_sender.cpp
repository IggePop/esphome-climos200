// Host-Test: Kodierung (BusEncoder) gegen den Dekoder aus dem Messmodus (BitDecoder)
#include "bit_decoder.h"
#include "bus_sender.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace esphome::climos;

static uint16_t crc16(const uint8_t *d, size_t n, uint16_t crc = 0) {
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

// Baut das Frame wie Climos::send_frame_
static std::vector<uint8_t> build(uint8_t mode) {
  std::vector<uint8_t> f = {0x01, 0x00, 0x85, 0x03, 0, 0, 0x28, 0x00, mode};
  uint16_t c = crc16(f.data(), 4);
  c = crc16(f.data() + 6, 3, c);
  f[4] = c & 0xFF;
  f[5] = c >> 8;
  return f;
}

static std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char b[4];
  for (auto x : v) { snprintf(b, sizeof b, "%02X ", x); s += b; }
  if (!s.empty()) s.pop_back();
  return s;
}

int main() {
  const char *expected[] = {
      "01 00 85 03 7C 1D 28 00 01", "01 00 85 03 1F 2D 28 00 02", "01 00 85 03 3E 3D 28 00 03",
      "01 00 85 03 D9 4D 28 00 04", "01 00 85 03 F8 5D 28 00 05", "01 00 85 03 9B 6D 28 00 06",
  };
  int fails = 0;
  BitDecoder dec;
  // Zeitbasis in µs; Ticks sind 0,1 µs. Vorlauf: Leitung High
  double t_us = 1000.0;
  dec.add_edge(static_cast<uint32_t>(t_us), true);
  uint32_t max_dur = 0, total_symbols = 0;

  for (uint8_t mode = 1; mode <= 6; mode++) {
    auto f = build(mode);
    if (hex(f) != expected[mode - 1]) {
      printf("FEHLER Frame %u: %s, erwartet %s\n", mode, hex(f).c_str(), expected[mode - 1]);
      fails++;
    } else {
      printf("OK Frame %u = Mitschnitt: %s\n", mode, hex(f).c_str());
    }

    std::vector<BusSymbol> syms;
    BusEncoder::encode(f.data(), f.size(), 2, syms);
    total_symbols += syms.size();
    // Symbole in Pegelwechsel umsetzen; Gesamtdauer prüfen
    uint64_t ticks = 0;
    bool level = true;
    for (size_t i = 0; i < syms.size(); i++) {
      const auto &s = syms[i];
      if (s.duration0 == 0 || (s.duration1 == 0 && i + 1 != syms.size())) {
        printf("FEHLER: Dauer 0 mitten im Frame (Symbol %zu)\n", i);
        fails++;
      }
      for (int half = 0; half < 2; half++) {
        const uint16_t d = half ? s.duration1 : s.duration0;
        const bool l = half ? s.level1 : s.level0;
        if (d == 0) continue;
        max_dur = d > max_dur ? d : max_dur;
        if (l != level) {
          dec.add_edge(static_cast<uint32_t>(t_us + ticks / 10.0), l);
          level = l;
        }
        ticks += d;
      }
    }
    if (!level) { printf("FEHLER: Frame endet nicht auf High\n"); fails++; }
    const uint64_t expect_ticks = (f.size() * 11 * 10000000ull + 4800) / 9600;
    if (ticks != expect_ticks) {
      printf("FEHLER Dauer: %llu Ticks, erwartet %llu\n", (unsigned long long)ticks, (unsigned long long)expect_ticks);
      fails++;
    }
    t_us += ticks / 10.0 + 160000.0;  // nächstes Frame nach 160 ms (wie im Sendefenster)
    dec.process(static_cast<uint32_t>(t_us));
  }
  dec.process(static_cast<uint32_t>(t_us + 100000));

  printf("\nDekoder: %u Frames OK, %u CRC-Fehler, %u Stoppbit-Fehler\n", dec.frames_ok(), dec.crc_errors(),
         dec.stop_errors());
  printf("9. Bit: erstes Byte %u gesetzt / %u nicht, weitere %u gesetzt / %u nicht\n", dec.first_marked(),
         dec.first_unmarked(), dec.other_marked(), dec.other_unmarked());
  printf("Abstand zwischen Bytes im Frame: min %d, max %d µs\n", dec.gap_inside_frame().min, dec.gap_inside_frame().max);
  for (auto &p : dec.patterns()) printf("Muster %s: %u, z.B. %s\n", p.first.c_str(), p.second.count, p.second.example.c_str());
  printf("RMT-Symbole: %u für 6 Frames, längster Abschnitt %u Ticks (max 32767)\n", total_symbols, max_dur);

  if (dec.frames_ok() != 6 || dec.crc_errors() || dec.stop_errors()) fails++;
  // Erstes Byte immer markiert; von den übrigen 8 Bytes je Frame genau eins (Byte 1)
  if (dec.first_marked() != 6 || dec.first_unmarked() != 0 || dec.other_marked() != 6) fails++;
  if (dec.gap_inside_frame().min < -1 || dec.gap_inside_frame().max > 1) fails++;
  printf("\n%s\n", fails ? "TEST FEHLGESCHLAGEN" : "ALLE TESTS OK");
  return fails ? 1 : 0;
}
