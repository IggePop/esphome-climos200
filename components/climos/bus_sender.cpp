#include "bus_sender.h"

#ifdef USE_ESP32
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#endif

namespace esphome {
namespace climos {

// Zeitpunkt der Bitgrenze k in Ticks, gerundet
static uint32_t bit_edge(uint32_t k) {
  const uint64_t num = static_cast<uint64_t>(k) * BusEncoder::RESOLUTION_HZ * 2 + BusEncoder::BAUD_RATE;
  return static_cast<uint32_t>(num / (2ull * BusEncoder::BAUD_RATE));
}

void BusEncoder::encode(const uint8_t *frame, size_t size, uint8_t marked_bytes, std::vector<BusSymbol> &out) {
  out.clear();
  // Pegelfolge: Abschnitte gleichen Pegels zusammenfassen (Start in Bitgrenzen)
  struct Run {
    uint8_t level;
    uint32_t start_bit;
  };
  std::vector<Run> runs;
  uint32_t bit = 0;
  const auto push = [&](uint8_t level) {
    if (runs.empty() || runs.back().level != level)
      runs.push_back({level, bit});
    bit++;
  };
  for (size_t i = 0; i < size; i++) {
    push(0);  // Start
    for (uint8_t b = 0; b < 8; b++)
      push((frame[i] >> b) & 1);
    push(i < marked_bytes ? 1 : 0);  // 9. Bit
    push(1);                         // Stopp
  }
  const uint32_t total_bits = bit;

  // Je zwei Abschnitte ergeben ein Symbol. Längster Abschnitt: 10 Bit = 10417 Ticks, passt in 15 Bit.
  for (size_t r = 0; r < runs.size(); r += 2) {
    BusSymbol sym{};
    const uint32_t end0 = r + 1 < runs.size() ? runs[r + 1].start_bit : total_bits;
    sym.level0 = runs[r].level;
    sym.duration0 = static_cast<uint16_t>(bit_edge(end0) - bit_edge(runs[r].start_bit));
    if (r + 1 < runs.size()) {
      const uint32_t end1 = r + 2 < runs.size() ? runs[r + 2].start_bit : total_bits;
      sym.level1 = runs[r + 1].level;
      sym.duration1 = static_cast<uint16_t>(bit_edge(end1) - bit_edge(runs[r + 1].start_bit));
    } else {
      // Letzter Abschnitt allein: Dauer 0 beendet die Übertragung, Pegel bleibt High
      sym.level1 = 1;
      sym.duration1 = 0;
    }
    out.push_back(sym);
  }
}

#ifdef USE_ESP32

static const char *const TAG = "CLIMOS_TX";

// Es gibt nur einen Sender; das Flag wird im RMT-Interrupt zurückgesetzt
static volatile bool tx_busy = false;

static bool IRAM_ATTR on_trans_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *) {
  tx_busy = false;
  return false;
}

bool BusSender::setup(int gpio_num) {
  rmt_tx_channel_config_t config{};
  config.gpio_num = static_cast<gpio_num_t>(gpio_num);
  config.clk_src = RMT_CLK_SRC_DEFAULT;
  config.resolution_hz = BusEncoder::RESOLUTION_HZ;
  config.mem_block_symbols = 128;  // zwei Speicherblöcke, Rest wird während des Sendens nachgefüllt
  config.trans_queue_depth = 1;
  config.flags.init_level = 1;     // Ruhepegel High von Anfang an: Adapter bleibt auf Empfang
  config.flags.io_loop_back = 1;   // Eingang des Pins bleibt aktiv (Messmodus kann den eigenen TX lesen)

  esp_err_t err = rmt_new_tx_channel(&config, &channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT-Kanal nicht angelegt: %s", esp_err_to_name(err));
    return false;
  }
  rmt_copy_encoder_config_t encoder_config{};
  err = rmt_new_copy_encoder(&encoder_config, &encoder_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT-Encoder nicht angelegt: %s", esp_err_to_name(err));
    return false;
  }
  rmt_tx_event_callbacks_t callbacks{};
  callbacks.on_trans_done = on_trans_done;
  err = rmt_tx_register_event_callbacks(channel_, &callbacks, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT-Callback nicht registriert: %s", esp_err_to_name(err));
    return false;
  }
  err = rmt_enable(channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT-Kanal nicht aktiviert: %s", esp_err_to_name(err));
    return false;
  }
  ready_ = true;
  return true;
}

bool BusSender::is_busy() { return tx_busy; }

bool BusSender::send(const uint8_t *frame, size_t size, uint8_t marked_bytes) {
  if (!ready_ || tx_busy)
    return false;

  std::vector<BusSymbol> encoded;
  BusEncoder::encode(frame, size, marked_bytes, encoded);
  symbols_.resize(encoded.size());
  for (size_t i = 0; i < encoded.size(); i++) {
    symbols_[i].duration0 = encoded[i].duration0;
    symbols_[i].level0 = encoded[i].level0;
    symbols_[i].duration1 = encoded[i].duration1;
    symbols_[i].level1 = encoded[i].level1;
  }

  rmt_transmit_config_t config{};
  config.loop_count = 0;
  config.flags.eot_level = 1;  // nach dem Frame wieder Ruhepegel
  tx_busy = true;
  const esp_err_t err =
      rmt_transmit(channel_, encoder_, symbols_.data(), symbols_.size() * sizeof(rmt_symbol_word_t), &config);
  if (err != ESP_OK) {
    tx_busy = false;
    ESP_LOGE(TAG, "Senden fehlgeschlagen: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

#else

bool BusSender::setup(int) { return false; }
bool BusSender::is_busy() { return false; }
bool BusSender::send(const uint8_t *, size_t, uint8_t) { return false; }

#endif

}  // namespace climos
}  // namespace esphome
