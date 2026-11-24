#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef USE_ESP32
#include <driver/rmt_tx.h>
#endif

namespace esphome {
namespace climos {

// Ein RMT-Symbol: zwei Abschnitte mit Pegel und Dauer in Ticks (je maximal 32767)
struct BusSymbol {
  uint16_t duration0;
  uint8_t level0;
  uint16_t duration1;
  uint8_t level1;
};

// Wandelt ein Frame in Pegelabschnitte für die RS485-Leitung (TX-Pin des Adapters):
// je Zeichen Start (0), 8 Datenbits LSB zuerst, 9. Bit, Stopp (1) = 11 Bit, ohne Pause dazwischen.
// Das 9. Bit ist auf den ersten `marked_bytes` Zeichen gesetzt (gemessen: die ersten zwei).
// Bitgrenzen werden über die ganze Länge gerundet, damit sich kein Fehler aufsummiert.
class BusEncoder {
 public:
  static const uint32_t BAUD_RATE = 9600;
  static const uint8_t BITS_PER_CHAR = 11;
  // 10 MHz: 80 MHz APB / 8, eine Bitzeit = 1041,67 Ticks
  static const uint32_t RESOLUTION_HZ = 10000000;

  static void encode(const uint8_t *frame, size_t size, uint8_t marked_bytes, std::vector<BusSymbol> &out);
  // Dauer eines Frames in µs
  static uint32_t frame_duration_us(size_t size) { return size * BITS_PER_CHAR * 1000000u / BAUD_RATE; }
};

// Sender über RMT. Der Pin liegt ab der Initialisierung fest auf High (Ruhepegel = Adapter empfängt)
// und kehrt nach jedem Frame dorthin zurück.
class BusSender {
 public:
  // false, wenn der RMT-Kanal nicht angelegt werden konnte
  bool setup(int gpio_num);
  bool is_ready() const { return ready_; }
  // true, solange ein Frame gesendet wird
  bool is_busy();
  // Frame unverändert senden (Kopf, CRC und Nutzdaten fertig). false, wenn nicht bereit oder noch beschäftigt.
  bool send(const uint8_t *frame, size_t size, uint8_t marked_bytes);
  size_t last_symbol_count() const { return symbols_.size(); }

 protected:
  bool ready_{false};
#ifdef USE_ESP32
  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};
  // Muss bis zum Ende der Übertragung gültig bleiben
  std::vector<rmt_symbol_word_t> symbols_;
#else
  std::vector<BusSymbol> symbols_;
#endif
};

}  // namespace climos
}  // namespace esphome
