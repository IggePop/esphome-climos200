#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/core/time.h"

#include "bit_decoder.h"
#include "bus_sender.h"
#include "schedule_codec.h"

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
  // t_som (Ablufttemperatur-Schwelle der Sommerlüftung) setzen wie das Bedienteil:
  // 01 00 85 08 [CRC] 1B 00 [0x1B aktuell] 41 00 [Wert in 0,1 °C]. Braucht den aktuellen Wert von 0x1B.
  bool send_summer_threshold(float celsius);
  // Dauer der Stoßlüftung setzen wie das Bedienteil: 01 00 85 03 [CRC] 3B 00 [Minuten], 15..120 in 5er-Schritten
  bool send_boost_duration(float minutes);
  // Anlage ein/aus wie das Bedienteil. Aus: 07 00 00, An: 07 00 01 0E 00 00 (löst Neuanmeldung aller Geräte aus).
  // Bestätigt, wenn 0x08 (Anlage an) den Wert meldet oder der Master 0x07 mit erstem Byte 00 wiederholt.
  bool send_system_power(bool on);
  // Wochenprogramm für die gewählten Tage setzen (Bitmaske, Bit 0 = Montag .. Bit 6 = Sonntag), alle Tage
  // bekommen dasselbe Programm. Text wie in der Anzeige, z.B. "00:00 Stufe 1, 07:00 Stufe 2, 20:00 Stufe 1".
  // Frames nacheinander (höchstens 6 Register je Frame), jedes muss vom Lüftergerät quittiert sein.
  bool send_schedule(uint8_t day_mask, const std::string &text);
  // Leistung von Stufe 1 oder 3 setzen (0x2E / 0x30), 17..100 % in 1-%-Schritten wie am Bedienteil
  bool send_stage_percent(uint8_t stage, float percent);
  // Frostschutz-Art setzen (0x7B): 0 = eco, 1 = sicher, 2 = Feuchte-WT
  bool send_frost_mode(uint8_t mode);
  // Laufzeit pro Stunde bei Abwesend setzen (0x3C): 15, 30 oder 45 min wie am Bedienteil
  bool send_away_interval(uint8_t minutes);
  // Filter-Intervall setzen (0x3D allein, wie das Bedienteil beim Einstellen): 30..180 Tage in 10er-Schritten
  bool send_filter_interval(float days);
  // Filter-Reset wie das Bedienteil: 0x3D und 0x3E in einem Frame, beide = aktuelles Intervall
  bool send_filter_reset();
  // Sommerlüftung freigeben/sperren: 0x1B mit aktuellem Wert und geändertem Bit 7 (braucht 0x1B)
  bool send_summer_enabled(bool enabled);
  // Datum und Uhrzeit des Bedienteils stellen (0x01, 0x02), Uhrzeit wird direkt vor dem Senden aktualisiert
  bool send_clock(const ESPTime &now);
  // Abweichung der Uhr der Anlage (aus dem Zeit-Frame des Masters) von now in Sekunden,
  // INT32_MAX wenn unbekannt, 86400 wenn das Datum abweicht
  int32_t panel_clock_deviation(const ESPTime &now) const;
  // Uhr stellen, wenn sie mehr als 60 s abweicht und gerade keine andere Anforderung läuft.
  // Sendet nichts, wenn die Uhr der Anlage oder die NTP-Zeit unbekannt ist. true = Stellen vorgemerkt.
  bool sync_clock_if_needed(const ESPTime &now);
  // Läuft gerade eine Anforderung (vorgemerkt, wartet auf Bestätigung oder mehrteilig)?
  bool is_busy() const { return pending_size_ > 0 || confirm_kind_ != CONFIRM_NONE || seq_active_; }
  // Komplett-Abzug anfordern wie das Bedienteil am Ende der Anmeldung (01 01 83 00 an den Master).
  // Einmal im Sendefenster, ohne Bestätigung und ohne Wiederholung. Der Master antwortet mit 01 00 83 00 und
  // dem Abzug, danach melden sich die Geräte neu: der Lüfter geht dabei etwa 13 s aus (2026-11-25).
  bool request_full_dump();
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
  // index 0..2 = Register 0x4A (sicher), 0x4B (eco), 0x4C (Feuchte-WT)
  void set_frost_threshold_sensor(uint8_t index, sensor::Sensor *sensor) { frost_threshold_sensors_[index] = sensor; }
  void set_frost_min_supply_sensor(sensor::Sensor *sensor) { frost_min_supply_sensor_ = sensor; }
  void set_schedule_text_sensor(uint8_t day, text_sensor::TextSensor *sensor) { schedule_text_sensors_[day] = sensor; }
  void set_summer_enabled_binary_sensor(binary_sensor::BinarySensor *sensor) { summer_enabled_binary_sensor_ = sensor; }
  // Zustand der letzten Umschaltung (vorgemerkt, gesendet, bestätigt, keine Bestätigung, ...)
  void set_send_status_text_sensor(text_sensor::TextSensor *sensor) { send_status_text_sensor_ = sensor; }
  void set_frost_mode_text_sensor(text_sensor::TextSensor *sensor) { frost_mode_text_sensor_ = sensor; }
  void set_panel_clock_text_sensor(text_sensor::TextSensor *sensor) { panel_clock_text_sensor_ = sensor; }

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
  // Wie wird ein gesendetes Frame bestätigt?
  // MODE: Gerät meldet die Betriebsart (0x29 von 08 oder 0x28 vom Bedienteil) -> "bestätigt"
  // ACK:  Lüftergerät quittiert unser Frame (87 mit unserer CRC nach 86 an 08) -> "angenommen",
  //       die gesendeten Werte werden übernommen. Quittung nur vom Bedienteil zählt nicht.
  // SYSTEM: Anlage an/aus, bestätigt über 0x08 oder das Master-Echo von 0x07; nur ein Versuch, 20 s Frist
  // ECHO: Datum/Uhrzeit, der Master übernimmt sie selbst und wiederholt sie (erstes Byte 00, Echo oder
  //       Zeit-Frame alle 5 s); 86/87 gibt es dafür nicht. Quittung von 08 zählt auch.
  enum ConfirmKind : uint8_t { CONFIRM_NONE, CONFIRM_MODE, CONFIRM_ACK, CONFIRM_SYSTEM, CONFIRM_ECHO };
  void check_echo_confirm_(uint16_t id, const uint8_t *value);
  bool queue_request_(const uint8_t *payload, uint8_t size, ConfirmKind kind, uint8_t mode, const std::string &label,
                      bool keep_sequence = false);
  void confirm_success_(ConfirmKind kind, const char *how);
  // Wartet eine Wiederholung dieses Typs (Bestätigung kam nach Ablauf der Frist)?
  bool retry_pending_(ConfirmKind kind) const {
    return confirm_attempt_ > 0 && pending_size_ > 0 && pending_kind_ == kind;
  }

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
  // Bestätigung des gesendeten Frames (siehe ConfirmKind)
  text_sensor::TextSensor *send_status_text_sensor_{nullptr};
  ConfirmKind pending_kind_{CONFIRM_NONE};  // vorgemerktes Frame
  uint8_t pending_mode_{0};                 // Betriebsart (nur CONFIRM_MODE)
  std::string pending_label_;               // z.B. "Stufe 2" oder "t_som 24.5 °C"
  ConfirmKind confirm_kind_{CONFIRM_NONE};  // gesendet, wartet auf Bestätigung
  uint8_t confirm_mode_{0};
  std::string confirm_label_;
  uint8_t confirm_attempt_{0};
  // Mehrteilige Anforderung (Wochenprogramm): Nutzdaten der Frames, aktuelles Frame, Name
  std::vector<std::vector<uint8_t>> seq_payloads_;
  size_t seq_index_{0};
  bool seq_active_{false};
  std::string seq_label_;
  // Frostschutz-Art und Uhr der Anlage
  text_sensor::TextSensor *frost_mode_text_sensor_{nullptr};
  text_sensor::TextSensor *panel_clock_text_sensor_{nullptr};
  uint8_t panel_date_[4]{};  // Wochentag (0 = Mo), Tag, Monat, Jahr (2-stellig)
  uint8_t panel_time_[3]{};  // Sekunde, Minute, Stunde
  bool has_panel_date_{false};
  bool has_panel_time_{false};
  uint32_t panel_time_ms_{0};  // millis() beim Empfang von 0x02
  void publish_panel_clock_();
  // Uhr stellen: Bezugszeit beim Vormerken, damit das Uhrzeit-Frame vor dem Senden aktualisiert werden kann
  bool clock_sync_active_{false};
  uint32_t clock_base_seconds_{0};  // Sekunden seit Mitternacht
  uint32_t clock_base_ms_{0};
  void refresh_pending_clock_();
  // Zusätzlich gespeicherte Werte (eigener Speicher, damit der bestehende nicht neu angelegt werden muss)
  struct ExtraStore {
    uint32_t valid;
    uint8_t data[8];
  };
  ESPPreferenceObject extra_pref_;
  ExtraStore extra_{};
  void setup_extra_settings_();
  bool remember_extra_setting_(uint16_t id, const uint8_t *value, uint8_t length);
  // Erstes Byte des gerade verarbeiteten Frames (00 = Echo des Masters)
  uint8_t frame_first_byte_{0};
  // An wen ging das letzte 86 (Bestätigung anfordern)? Die folgende 87 kommt von diesem Gerät.
  uint8_t last_ack_request_dst_{0};
  // Einstellwert wurde in diesem Durchlauf nur vom Bedienteil quittiert (zählt nicht als Bestätigung)
  bool acked_by_panel_only_{false};
  // Aktueller Wert von 0x1B (Freigaben), muss beim Senden von t_som mitgeschickt werden
  uint8_t features_raw_[2]{};
  bool has_features_{false};
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
  sensor::Sensor *frost_min_supply_sensor_{nullptr};
  // Filter-Intervall in Tagen aus 0x3D (für den Filter-Reset)
  uint16_t filter_interval_days_{0};
  bool has_filter_interval_{false};
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
