// Host-Test: Wochenprogramm Text -> Frames gegen Mitschnitte des Bedienteils (2026-11-25)
#include "schedule_codec.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace esphome::climos;

static uint16_t crc16(const std::vector<uint8_t> &d, uint16_t crc = 0) {
  for (uint8_t b : d) {
    crc ^= b << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

static std::string frame_hex(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> head = {0x01, 0x00, 0x85, static_cast<uint8_t>(payload.size())};
  std::vector<uint8_t> all = head;
  all.insert(all.end(), payload.begin(), payload.end());
  const uint16_t c = crc16(all);
  std::vector<uint8_t> f = head;
  f.push_back(c & 0xFF);
  f.push_back(c >> 8);
  f.insert(f.end(), payload.begin(), payload.end());
  std::string s;
  char b[4];
  for (auto x : f) { snprintf(b, sizeof b, "%02X ", x); s += b; }
  s.pop_back();
  return s;
}

static int fails = 0;

static void check_group(const char *name, uint8_t mask, const std::string &text, const std::vector<std::string> &expected) {
  uint8_t levels[SCHEDULE_SLOTS];
  std::string err;
  if (!parse_schedule_text(text, levels, err)) {
    printf("FEHLER %s: Text nicht gelesen: %s\n", name, err.c_str());
    fails++;
    return;
  }
  auto payloads = build_schedule_payloads(mask, levels);
  if (payloads.size() != expected.size()) {
    printf("FEHLER %s: %zu Frames, erwartet %zu\n", name, payloads.size(), expected.size());
    fails++;
  }
  for (size_t i = 0; i < payloads.size() && i < expected.size(); i++) {
    const std::string got = frame_hex(payloads[i]);
    if (got != expected[i]) {
      printf("FEHLER %s Frame %zu:\n  ist:  %s\n  soll: %s\n", name, i + 1, got.c_str(), expected[i].c_str());
      fails++;
    } else {
      printf("OK %s Frame %zu = Mitschnitt (%s ...)\n", name, i + 1, got.substr(0, 26).c_str());
    }
  }
}

static void check_error(const std::string &text) {
  uint8_t levels[SCHEDULE_SLOTS];
  std::string err;
  if (parse_schedule_text(text, levels, err)) {
    printf("FEHLER: \"%s\" wurde angenommen\n", text.c_str());
    fails++;
  } else {
    printf("OK abgelehnt: \"%s\" -> %s\n", text.c_str(), err.c_str());
  }
}

int main() {
  // 20:03:43, Mo-Fr 00:00 Stufe 1, 07:00 Stufe 2, 20:00 Stufe 1
  check_group("Mo-Fr 20:00", SCHEDULE_MON_FRI, "00:00 Stufe 1, 07:00 Stufe 2, 20:00 Stufe 1",
              {"01 00 85 3C A3 43 64 00 55 55 55 55 55 55 55 AA 65 00 AA AA AA AA AA AA AA AA 66 00 AA AA AA AA 55 55 55 55 "
               "67 00 55 55 55 55 55 55 55 AA 68 00 AA AA AA AA AA AA AA AA 69 00 AA AA AA AA 55 55 55 55",
               "01 00 85 3C 46 C0 6A 00 55 55 55 55 55 55 55 AA 6B 00 AA AA AA AA AA AA AA AA 6C 00 AA AA AA AA 55 55 55 55 "
               "6D 00 55 55 55 55 55 55 55 AA 6E 00 AA AA AA AA AA AA AA AA 6F 00 AA AA AA AA 55 55 55 55",
               "01 00 85 1E EB DC 70 00 55 55 55 55 55 55 55 AA 71 00 AA AA AA AA AA AA AA AA 72 00 AA AA AA AA 55 55 55 55"});
  // 20:03:11, Mo-Fr 00:00 Stufe 1, 07:00 Stufe 2, 19:30 Stufe 1
  check_group("Mo-Fr 19:30", SCHEDULE_MON_FRI, "00:00 Stufe 1, 07:00 Stufe 2, 19:30 Stufe 1",
              {"01 00 85 3C A7 9A 64 00 55 55 55 55 55 55 55 AA 65 00 AA AA AA AA AA AA AA AA 66 00 AA AA AA 5A 55 55 55 55 "
               "67 00 55 55 55 55 55 55 55 AA 68 00 AA AA AA AA AA AA AA AA 69 00 AA AA AA 5A 55 55 55 55",
               "01 00 85 3C 42 19 6A 00 55 55 55 55 55 55 55 AA 6B 00 AA AA AA AA AA AA AA AA 6C 00 AA AA AA 5A 55 55 55 55 "
               "6D 00 55 55 55 55 55 55 55 AA 6E 00 AA AA AA AA AA AA AA AA 6F 00 AA AA AA 5A 55 55 55 55",
               "01 00 85 1E BD E3 70 00 55 55 55 55 55 55 55 AA 71 00 AA AA AA AA AA AA AA AA 72 00 AA AA AA 5A 55 55 55 55"});

  // Sa-So: ein Frame mit 0x73..0x78 (kein Mitschnitt, nur Aufbau prüfen)
  uint8_t levels[SCHEDULE_SLOTS];
  std::string err;
  parse_schedule_text("00:00 Stufe 1, 08:00 Stufe 2, 20:00 Stufe 1", levels, err);
  auto we = build_schedule_payloads(SCHEDULE_SAT_SUN, levels);
  // Einzelner Tag: Mittwoch = 1 Frame mit 0x6A..0x6C (3 Register, Länge 0x1E)
  auto mi = build_schedule_payloads(1u << 2, levels);
  if (mi.size() != 1 || mi[0].size() != 30 || mi[0][0] != 0x6A || mi[0][20] != 0x6C) {
    printf("FEHLER Mittwoch: %zu Frames\n", mi.size());
    fails++;
  } else {
    printf("OK Mittwoch: 1 Frame, 0x6A..0x6C, Länge 0x1E\n");
  }
  // Mo + Fr: 6 Register in 1 Frame, 0x64..0x66 und 0x70..0x72
  auto mofr = build_schedule_payloads(0x11, levels);
  if (mofr.size() != 1 || mofr[0][0] != 0x64 || mofr[0][30] != 0x70) {
    printf("FEHLER Mo+Fr\n");
    fails++;
  } else {
    printf("OK Mo+Fr: 1 Frame, 0x64..0x66 + 0x70..0x72\n");
  }
  if (we.size() != 1 || we[0].size() != 60 || we[0][0] != 0x73 || we[0][50] != 0x78) {
    printf("FEHLER Sa-So: %zu Frames\n", we.size());
    fails++;
  } else {
    printf("OK Sa-So: 1 Frame, 0x73..0x78, Länge 0x3C\n");
  }

  // Eingaben, die abgelehnt werden müssen
  check_error("");
  check_error("07:00 Stufe 2");
  check_error("00:00 Stufe 1, 07:10 Stufe 2");
  check_error("00:00 Stufe 1, 20:00 Stufe 2, 07:00 Stufe 1");
  check_error("00:00 Stufe 4");
  check_error("00:00 Stufe 1,, 07:00 Stufe 2");
  check_error("00:00 Stufe 1, 24:00 aus");

  // Groß/klein und Leerzeichen
  if (!parse_schedule_text("00:00 aus,  06:30 stufe 3 ,22:00 Stufe 1", levels, err) || levels[0] != 0 ||
      levels[26] != 3 || levels[88] != 1 || levels[95] != 1) {
    printf("FEHLER: freie Schreibweise nicht korrekt gelesen\n");
    fails++;
  } else {
    printf("OK freie Schreibweise\n");
  }

  printf("\n%s\n", fails ? "TEST FEHLGESCHLAGEN" : "ALLE TESTS OK");
  return fails ? 1 : 0;
}
