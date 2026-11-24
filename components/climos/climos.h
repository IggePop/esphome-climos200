#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#include "bit_decoder.h"
#include "bus_sender.h"

#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace climos {

// Frame: [b0][dst][cmd][len][crc_lo][crc_hi][payload: len & 0x7F bytes]
// CRC-16/XMODEM über die 4 Kopfbytes + Payload.
static const size_t HEADER_SIZE = 6;
static const uint8_t NUM_TEMPERATURES = 4;
static const uint8_t NUM_FAN_STAGES = 3;
static const uint8_t NUM_WEEKDAYS = 7;
static const uint8_t SCHEDULE_REGISTER_SIZE = 8;
static const uint8_t NUM_FROST_THRESHOLDS = 3;
// Anzahl der gespeicherten Register (PERSISTED_IDS) und Platz für ihre Rohwerte
static const uint8_t NUM_PERSISTED = 46;
static const size_t SETTINGS_STORE_SIZE = 256;

struct SettingsStore {
  uint64_t valid;  // Bit n: Register PERSISTED_IDS[n] vorhanden
  uint8_t data[SETTINGS_STORE_SIZE];
};

// Pegelwechsel aus dem Interrupt: Bit 0 = Pegel, Rest = Zeit in µs
struct EdgeStore {
  uint32_t *edges{nullptr};
  volatile uint32_t head{0};
  volatile uint32_t tail{0};
  volatile uint32_t overflows{0};
  ISRInternalGPIOPin pin;
  static void gpio_intr(EdgeStore *store);
};

class Climos : public Component, public uart::UARTDevice {
 public:
  void set_bit_capture_pin(InternalGPIOPin *pin) { bit_capture_pin_ = pin; }
  // Sendepin (TXD des Adapters). Ohne ihn wird kein Sender angelegt.
  void set_tx_pin(int pin) { tx_pin_ = pin; }
  // Schutzschalter, nach dem Start immer aus
  void set_write_enabled(bool enabled);
  bool is_write_enabled() const { return write_enabled_; }
  // Betriebsart 1..6 senden wie das Bedienteil: 01 00 85 03 [CRC] 28 00 [Code].
  // Das Frame wird vorgemerkt und im nächsten Fenster nach 01 FF 84 00 gesendet
  // (im Testsegment ohne Busverkehr sofort). true = vorgemerkt.
  bool send_operating_mode(uint8_t mode);
  // Die zuletzt gesehene (bzw. gespeicherte) Betriebsart senden, ändert also nichts
  bool send_current_operating_mode();
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_temperature_sensor(uint8_t index, sensor::Sensor *sensor) { temperature_sensors_[index] = sensor; }
  void set_fan_stage_text_sensor(text_sensor::TextSensor *sensor) { fan_stage_text_sensor_ = sensor; }
  void set_filter_remaining_sensor(sensor::Sensor *sensor) { filter_remaining_sensor_ = sensor; }
  void set_operating_hours_sensor(sensor::Sensor *sensor) { operating_hours_sensor_ = sensor; }
  void set_fan_operating_hours_sensor(sensor::Sensor *sensor) { fan_operating_hours_sensor_ = sensor; }
  void set_status_1a_sensor(sensor::Sensor *sensor) { status_1a_sensor_ = sensor; }
  void set_filter_interval_sensor(sensor::Sensor *sensor) { filter_interval_sensor_ = sensor; }
  void set_operating_mode_text_sensor(text_sensor::TextSensor *sensor) { operating_mode_text_sensor_ = sensor; }
  void set_last_message_text_sensor(text_sensor::TextSensor *sensor) { last_message_text_sensor_ = sensor; }
  void set_bus_version_text_sensor(text_sensor::TextSensor *sensor) { bus_version_text_sensor_ = sensor; }
  void set_sw_master_text_sensor(text_sensor::TextSensor *sensor) { sw_master_text_sensor_ = sensor; }
  void set_sw_slave_text_sensor(text_sensor::TextSensor *sensor) { sw_slave_text_sensor_ = sensor; }
  void set_sw_tft_text_sensor(text_sensor::TextSensor *sensor) { sw_tft_text_sensor_ = sensor; }
  void set_system_on_binary_sensor(binary_sensor::BinarySensor *sensor) { system_on_binary_sensor_ = sensor; }
  void set_summer_ventilation_binary_sensor(binary_sensor::BinarySensor *sensor) {
    summer_ventilation_binary_sensor_ = sensor;
  }
  void set_exhaust_fan_binary_sensor(binary_sensor::BinarySensor *sensor) { exhaust_fan_binary_sensor_ = sensor; }
  void set_summer_threshold_sensor(sensor::Sensor *sensor) { summer_threshold_sensor_ = sensor; }
  void set_summer_min_outside_sensor(sensor::Sensor *sensor) { summer_min_outside_sensor_ = sensor; }
  void set_fan_level_raw_sensor(sensor::Sensor *sensor) { fan_level_raw_sensor_ = sensor; }
  void set_summer_hysteresis_sensor(sensor::Sensor *sensor) { summer_hysteresis_sensor_ = sensor; }
  void set_error_code_sensor(sensor::Sensor *sensor) { error_code_sensor_ = sensor; }
  void set_stage_percent_sensor(uint8_t index, sensor::Sensor *sensor) { stage_percent_sensors_[index] = sensor; }
  void set_boost_duration_sensor(sensor::Sensor *sensor) { boost_duration_sensor_ = sensor; }
  void set_away_interval_sensor(sensor::Sensor *sensor) { away_interval_sensor_ = sensor; }
  void set_debalance_sensor(uint8_t index, sensor::Sensor *sensor) { debalance_sensors_[index] = sensor; }
  void set_frost_threshold_sensor(uint8_t index, sensor::Sensor *sensor) { frost_threshold_sensors_[index] = sensor; }
  void set_schedule_text_sensor(uint8_t day, text_sensor::TextSensor *sensor) { schedule_text_sensors_[day] = sensor; }
  void set_summer_enabled_binary_sensor(binary_sensor::BinarySensor *sensor) { summer_enabled_binary_sensor_ = sensor; }
  // Zustand der letzten Umschaltung (vorgemerkt, gesendet, bestätigt, keine Bestätigung, ...)
  void set_send_status_text_sensor(text_sensor::TextSensor *sensor) { send_status_text_sensor_ = sensor; }

 protected:
  bool try_parse_frame_();
  void handle_frame_(const uint8_t *frame, size_t size);
  void parse_records_(const uint8_t *payload, size_t size);
  void handle_object_(uint16_t id, const uint8_t *value, uint8_t length);
  void remember_setting_(uint16_t id, const uint8_t *value, uint8_t length);
  void setup_bit_capture_();
  void loop_bit_capture_();
  void log_bit_capture_();
  static uint8_t object_length_(uint16_t id);
  void publish_if_changed_(sensor::Sensor *sens, float value);
  void publish_if_changed_(text_sensor::TextSensor *sens, const std::string &value);
  void publish_if_changed_(binary_sensor::BinarySensor *sens, bool value);
  void publish_schedule_day_(uint8_t day);
  static std::string mode_to_string_(uint8_t mode);
  static uint32_t duration_to_minutes_(const uint8_t *value);
  void dump_frame_(const char *prefix, const uint8_t *data, size_t size);
  static std::string to_hex_(const uint8_t *data, size_t size);
  static uint16_t crc16_xmodem_(const uint8_t *data, size_t size, uint16_t crc = 0);
  bool queue_frame_(uint8_t first, uint8_t dst, uint8_t cmd, const uint8_t *payload, uint8_t payload_size);
  void loop_send_();
  bool transmit_pending_(const char *how, uint32_t now);
  void clear_pending_();
  void loop_confirm_(uint32_t now);
  void publish_send_status_(const char *what);

 private:
  int tx_pin_{-1};
  BusSender sender_;
  bool write_enabled_{false};
  uint32_t last_send_time_{0};
  bool has_sent_{false};
  uint8_t current_mode_{0};
  // Vorgemerktes Frame (höchstens eins)
  uint8_t pending_frame_[HEADER_SIZE + 0x7F]{};
  size_t pending_size_{0};
  uint32_t pending_since_{0};
  HighFrequencyLoopRequester high_freq_;
  // Sendefenster: Zeitpunkt des letzten 01 FF 84 00 und Bytezähler direkt danach
  uint32_t rx_bytes_{0};
  bool ff_poll_seen_{false};
  uint32_t ff_poll_time_{0};
  uint32_t ff_poll_rx_bytes_{0};
  // Beobachtung der Reaktion nach dem Senden
  uint32_t watch_until_{0};
  bool watching_{false};
  bool watch_hit_{false};
  std::string last_watch_log_;
  // Bestätigung einer gesendeten Betriebsart: 0x29 von 08 oder 0x28 vom Bedienteil mit diesem Code
  text_sensor::TextSensor *send_status_text_sensor_{nullptr};
  uint8_t pending_mode_{0};    // Betriebsart des vorgemerkten Frames (0 = keine)
  uint8_t confirm_mode_{0};    // Betriebsart, auf deren Bestätigung gewartet wird (0 = keine)
  uint8_t confirm_attempt_{0};
  uint32_t confirm_deadline_{0};
  uint8_t sent_frame_[HEADER_SIZE + 0x7F]{};
  size_t sent_size_{0};
  std::vector<uint8_t> rx_buffer_;
  // Messmodus 9. Bit, nur aktiv wenn bit_capture_pin konfiguriert ist
  InternalGPIOPin *bit_capture_pin_{nullptr};
  EdgeStore edge_store_;
  BitDecoder bit_decoder_;
  bool bit_capture_active_{false};
  uint32_t last_bit_capture_log_{0};
  uint32_t last_byte_time_{0};
  uint8_t last_polled_{0};
  uint32_t frames_ok_{0};
  uint32_t crc_errors_{0};
  uint32_t last_stats_time_{0};
  // Letzte Payload je Frame-Art, um nur Änderungen auf INFO zu loggen
  std::map<std::string, std::vector<uint8_t>> last_payloads_;
  sensor::Sensor *temperature_sensors_[NUM_TEMPERATURES]{};
  text_sensor::TextSensor *fan_stage_text_sensor_{nullptr};
  sensor::Sensor *filter_remaining_sensor_{nullptr};
  sensor::Sensor *operating_hours_sensor_{nullptr};
  sensor::Sensor *fan_operating_hours_sensor_{nullptr};
  sensor::Sensor *status_1a_sensor_{nullptr};
  sensor::Sensor *filter_interval_sensor_{nullptr};
  text_sensor::TextSensor *operating_mode_text_sensor_{nullptr};
  text_sensor::TextSensor *last_message_text_sensor_{nullptr};
  text_sensor::TextSensor *bus_version_text_sensor_{nullptr};
  text_sensor::TextSensor *sw_master_text_sensor_{nullptr};
  text_sensor::TextSensor *sw_slave_text_sensor_{nullptr};
  text_sensor::TextSensor *sw_tft_text_sensor_{nullptr};
  binary_sensor::BinarySensor *system_on_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *summer_ventilation_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *exhaust_fan_binary_sensor_{nullptr};
  sensor::Sensor *summer_threshold_sensor_{nullptr};
  sensor::Sensor *summer_min_outside_sensor_{nullptr};
  sensor::Sensor *fan_level_raw_sensor_{nullptr};
  sensor::Sensor *summer_hysteresis_sensor_{nullptr};
  sensor::Sensor *error_code_sensor_{nullptr};
  sensor::Sensor *stage_percent_sensors_[NUM_FAN_STAGES]{};
  sensor::Sensor *boost_duration_sensor_{nullptr};
  sensor::Sensor *away_interval_sensor_{nullptr};
  sensor::Sensor *debalance_sensors_[NUM_FAN_STAGES]{};
  sensor::Sensor *frost_threshold_sensors_[NUM_FROST_THRESHOLDS]{};
  // Gespeicherte Einstellwerte (Rohbytes je Register)
  ESPPreferenceObject settings_pref_;
  SettingsStore settings_{};
  size_t settings_offsets_[NUM_PERSISTED]{};
  bool settings_dirty_{false};
  bool persistence_ok_{true};
  text_sensor::TextSensor *schedule_text_sensors_[NUM_WEEKDAYS]{};
  // Wochenprogramm, 3 Register je Tag; Bit n in schedule_received_ = Register n empfangen
  uint8_t schedule_[NUM_WEEKDAYS * 3][SCHEDULE_REGISTER_SIZE]{};
  uint32_t schedule_received_{0};
  binary_sensor::BinarySensor *summer_enabled_binary_sensor_{nullptr};
  // Zwischenspeicher für Meldung 1 (Objekte 0x0A..0x0C)
  uint8_t message_code_{0};
  uint8_t message_date_[4]{};
};

}  // namespace climos
}  // namespace esphome
