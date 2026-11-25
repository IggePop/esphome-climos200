#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace climos {

// Wochenprogramm: je Tag 3 Register (00-08, 08-16, 16-24 Uhr) zu 8 Byte, 2 Bit je Viertelstunde,
// erste Viertelstunde in den unteren Bits. Wert 0 = aus, 1..3 = Stufe 1..3.
// Register 0x64 + 3*Tag + n, Tag 0 = Montag .. 6 = Sonntag.
static const uint8_t SCHEDULE_SLOTS = 96;
static const uint16_t SCHEDULE_FIRST_REGISTER = 0x0064;
// Das Bedienteil schickt höchstens 6 Register je Frame (Mitschnitt 2026-11-25: Mo-Fr = 6 + 6 + 3)
static const uint8_t SCHEDULE_REGISTERS_PER_FRAME = 6;

// Text wie in der Anzeige, z.B. "00:00 Stufe 1, 07:00 Stufe 2, 20:00 Stufe 1".
// Zeiten in Viertelstunden, aufsteigend, erster Eintrag 00:00. Stufen: "aus", "Stufe 1".."Stufe 3".
// false bei Fehler, dann steht in error der Grund.
bool parse_schedule_text(const std::string &text, uint8_t levels[SCHEDULE_SLOTS], std::string &error);

// 96 Stufen -> 3 Register zu 8 Byte
void encode_schedule_day(const uint8_t levels[SCHEDULE_SLOTS], uint8_t regs[3][8]);

// Tagesauswahl als Bitmaske, Bit 0 = Montag .. Bit 6 = Sonntag
static const uint8_t SCHEDULE_MON_FRI = 0x1F;
static const uint8_t SCHEDULE_SAT_SUN = 0x60;

// Nutzdaten wie vom Bedienteil: die 3 Register jedes gewählten Tags in Tagesreihenfolge, höchstens
// 6 Register je Frame (Mo-Fr = 3 Frames, Sa-So = 1 Frame, ein Tag = 1 Frame mit 3 Registern).
// Alle gewählten Tage bekommen dasselbe Programm.
std::vector<std::vector<uint8_t>> build_schedule_payloads(uint8_t day_mask, const uint8_t levels[SCHEDULE_SLOTS]);

}  // namespace climos
}  // namespace esphome
