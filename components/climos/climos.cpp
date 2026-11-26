#include "climos.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace climos {

static const char *const TAG = "CLIMOS_BUS";

static const uint8_t CMD_POLL = 0x84;
static const uint8_t CMD_DATA = 0x85;
static const uint8_t CMD_CONFIRM_REQUEST = 0x86;
static const uint8_t CMD_ACK = 0x87;
static const uint8_t CMD_TIME = 0x00;
static const uint8_t OBJ_UNKNOWN_55 = 0x55;

// Objekt-IDs in der Payload von 0x85-Frames
static const uint16_t OBJ_STATUS_1A = 0x001A;
static const uint16_t OBJ_SYSTEM_ON = 0x0008;
static const uint16_t OBJ_SUMMER_THRESHOLD = 0x0041;      // t_som, Ablufttemperatur-Schwelle
static const uint16_t OBJ_SUMMER_MIN_OUTSIDE = 0x0049;    // minimale Außenlufttemperatur
static const uint16_t OBJ_FAN_LEVEL_RAW = 0x0044;         // unbekannt, reagiert nicht auf die Lüfterstufe
static const uint16_t OBJ_SUMMER_HYSTERESIS = 0x0043;     // H_som, 0.1 K
static const uint16_t OBJ_ERROR_CODE = 0x000E;
static const uint16_t OBJ_FEATURES = 0x001B;              // Freigaben, Bit 7 = Sommerlüftung freigegeben
static const uint8_t FEATURE_SUMMER_VENTILATION = 0x80;
// Einstellwerte, nur im Komplett-Abzug oder wenn sie am Bedienteil geändert werden
static const uint16_t OBJ_FIRST_STAGE_PERCENT = 0x002E;  // Stufen 1..3 in %
static const uint16_t OBJ_BOOST_DURATION = 0x003B;       // Minuten
static const uint16_t OBJ_AWAY_INTERVAL = 0x003C;        // Minuten Stufe 1 pro Stunde
static const uint16_t OBJ_FIRST_DEBALANCE = 0x0031;      // Disbalance Stufen 1..3 in %, vorzeichenbehaftet
static const uint16_t OBJ_FIRST_FROST_THRESHOLD = 0x004A;  // Frostschutz-Schwellen, 0.1 °C, vorzeichenbehaftet
// Wochenprogramm: 0x64..0x78, je Tag drei Register (00-08, 08-16, 16-24 Uhr) zu 8 Byte,
// 2 Bit je Viertelstunde, erste Viertelstunde in den unteren Bits (00 aus, 01..11 Stufe 1..3)
static const uint16_t OBJ_FIRST_SCHEDULE = 0x0064;
static const uint8_t SCHEDULE_REGISTERS_PER_DAY = 3;
static const size_t MAX_SCHEDULE_TEXT = 250;
// Bits in Status 0x1A (aus Mitschnitten abgeleitet)
static const uint8_t STATUS_EXHAUST_FAN = 0x08;
static const uint8_t STATUS_SUMMER_VENTILATION = 0x20;
static const uint16_t OBJ_OPERATING_TIME = 0x0026;
static const uint16_t OBJ_FAN_STAGE = 0x0027;
static const uint16_t OBJ_FILTER_REMAINING = 0x003E;
static const uint16_t OBJ_FIRST_TEMPERATURE = 0x0081;
static const uint16_t OBJ_FAN_OPERATING_TIME = 0x0085;
static const uint16_t OBJ_BUS_VERSION = 0x0000;
static const uint16_t OBJ_SW_MASTER = 0x0003;
static const uint16_t OBJ_MESSAGE_CODE = 0x000A;
static const uint16_t OBJ_MESSAGE_DATE = 0x000B;
static const uint16_t OBJ_MESSAGE_TIME = 0x000C;
static const uint16_t OBJ_SW_TFT = 0x000D;
static const uint16_t OBJ_SW_SLAVE = 0x0019;
static const uint16_t OBJ_MODE = 0x0028;  // laufende Betriebsart; 0x29 ist die Rückfall-Betriebsart nach Stoßlüftung
static const uint16_t OBJ_FILTER_INTERVAL = 0x003D;
static const uint8_t MESSAGE_OUTSIDE_TEMP_LOW = 0x86;

// Payload = Folge aus [ID lo][ID hi][Wert]; die Wertlänge hängt von der ID ab
// (aus dem Komplett-Abzug beim Anmelden des Bedienteils ermittelt)
struct ObjectLength {
  uint16_t id;
  uint8_t length;
};
static const ObjectLength OBJECT_LENGTHS[] = {
    {0x0000, 3}, {0x0001, 4}, {0x0002, 3}, {0x0003, 11}, {0x0004, 1}, {0x0005, 2}, {0x0006, 1}, {0x0007, 1}, {0x0008, 1},
    {0x0009, 16}, {0x000A, 16}, {0x000B, 4}, {0x000C, 3}, {0x000D, 11}, {0x000E, 1}, {0x000F, 1},
    {0x0012, 1}, {0x0015, 1}, {0x0018, 1}, {0x0019, 11}, {0x001A, 1}, {0x001B, 2}, {0x001E, 1},
    {0x0021, 1}, {0x0024, 1}, {0x0025, 1}, {0x0026, 5}, {0x0027, 1}, {0x0028, 1}, {0x0029, 1},
    {0x002A, 1}, {0x002B, 1}, {0x002C, 1}, {0x002D, 1}, {0x002E, 1}, {0x002F, 1}, {0x0030, 1},
    {0x0031, 1}, {0x0032, 1}, {0x0033, 1}, {0x0034, 1}, {0x0035, 1}, {0x0036, 1}, {0x0037, 1},
    {0x0038, 1}, {0x0039, 1}, {0x003A, 1}, {0x003B, 1}, {0x003C, 1}, {0x003D, 5}, {0x003E, 5},
    {0x003F, 5}, {0x0040, 1}, {0x0041, 2}, {0x0042, 2}, {0x0043, 1}, {0x0044, 1}, {0x0045, 1},
    {0x0046, 1}, {0x0047, 2}, {0x0048, 1}, {0x0049, 2}, {0x004A, 2}, {0x004B, 2}, {0x004C, 2},
    {0x004D, 1}, {0x004E, 1}, {0x004F, 1}, {0x0050, 1}, {0x0051, 1}, {0x0052, 1}, {0x0053, 1},
    {0x0054, 1}, {0x0055, 2}, {0x0056, 2}, {0x0057, 1}, {0x0058, 1}, {0x0059, 1}, {0x005A, 2},
    {0x005B, 2}, {0x005C, 1}, {0x005D, 1}, {0x005E, 1}, {0x005F, 2}, {0x0060, 2}, {0x0061, 1},
    {0x0062, 1}, {0x0063, 2}, {0x0064, 8}, {0x0065, 8}, {0x0066, 8}, {0x0067, 8}, {0x0068, 8}, {0x0069, 8},
    {0x006A, 8}, {0x006B, 8}, {0x006C, 8}, {0x006D, 8}, {0x006E, 8}, {0x006F, 8}, {0x0070, 8},
    {0x0071, 8}, {0x0072, 8}, {0x0073, 8}, {0x0074, 8}, {0x0075, 8}, {0x0076, 8}, {0x0077, 8},
    {0x0078, 8}, {0x0079, 1}, {0x007A, 1}, {0x007B, 1}, {0x007C, 1}, {0x007D, 1}, {0x007E, 1},
    {0x007F, 1}, {0x0080, 1}, {0x0081, 2}, {0x0082, 2}, {0x0083, 2}, {0x0084, 2}, {0x0085, 5},
    {0x0086, 1}, {0x0087, 1}, {0x0088, 16}, {0x0089, 4}, {0x008A, 3}, {0x008B, 16}, {0x008C, 4},
    {0x008D, 3}, {0x008E, 1}, {0x0091, 1}, {0x0094, 1},
};
// Werte, die nur im Komplett-Abzug oder bei Änderung kommen: im Flash halten und beim Start wiederherstellen.
// Reihenfolge = Reihenfolge der Wiederherstellung (Meldung 0x0A..0x0C muss zusammenbleiben).
static const uint16_t PERSISTED_IDS[] = {
    0x0000, 0x0003, 0x000A, 0x000B, 0x000C, 0x000D, 0x0019, 0x001B, 0x0027, 0x0028, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x003B, 0x003C, 0x003D, 0x0041, 0x0043, 0x0049, 0x004A, 0x004B,
    0x004C, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E,
    0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078,
};
static_assert(sizeof(PERSISTED_IDS) / sizeof(PERSISTED_IDS[0]) == NUM_PERSISTED, "NUM_PERSISTED anpassen");

// Unvollständiges Frame verwerfen, wenn so lange nichts mehr kommt
static const uint32_t STALE_TIMEOUT_MS = 50;
static const uint32_t STATS_INTERVAL_MS = 60000;
// Messmodus: Ringpuffer (Zweierpotenz), Ausgabe alle 30 s, Ende nach etwa 5 Minuten Busverkehr
static const uint32_t EDGE_BUFFER_SIZE = 4096;
static const uint32_t BIT_CAPTURE_LOG_INTERVAL_MS = 30000;
static const uint32_t BIT_CAPTURE_MAX_FRAMES = 6000;

// Senden am Bus nur im Fenster nach 01 FF 84 00: dort antwortet niemand, danach 158-163 ms Stille
// (gemessen 2026-10-31). Gesendet wird 15..40 ms nach dem Einlesen des Polls, wenn seitdem kein Byte
// kam. Ein Frame dauert etwa 10 ms, ist also spätestens rund 90 ms nach dem Poll fertig.
static const uint8_t ADDR_BROADCAST = 0xFF;
static const uint32_t SEND_WINDOW_MIN_MS = 15;
static const uint32_t SEND_WINDOW_MAX_MS = 40;
// Vorgemerktes Frame verfällt, wenn kein Fenster kam (Fenster etwa alle 9 s)
static const uint32_t SEND_PENDING_TIMEOUT_MS = 30000;
// Testsegment ohne KWL: sofort senden, wenn der Bus so lange still war. Am echten Bus kommt
// etwa alle 1,5 ms ein Frame, dort greift dieser Weg also nie.
static const uint32_t SEND_BUS_SILENCE_MS = 2000;
static const uint32_t SEND_MIN_INTERVAL_MS = 1000;
// Nach dem Senden so lange Frames mit 0x28/0x29 und Frames mit erstem Byte 00 loggen
static const uint32_t SEND_WATCH_MS = 10000;
static const uint16_t OBJ_MODE_FALLBACK = 0x0029;
// Bestätigung: so lange auf 0x29 von 08 bzw. 0x28 vom Bedienteil warten, dann wiederholen
static const uint32_t SEND_CONFIRM_MS = 3000;
static const uint8_t SEND_MAX_ATTEMPTS = 3;
// Einstellwerte: der Master fragt die Quittung oft nur beim Bedienteil ab, daher mehr Versuche
static const uint8_t SEND_MAX_ATTEMPTS_ACK = 8;
// Datum/Uhrzeit: Bestätigung spätestens mit dem nächsten Zeit-Frame (alle 5 s)
static const uint32_t ECHO_CONFIRM_MS = 7000;
static const int32_t CLOCK_CONFIRM_TOLERANCE_S = 10;
// Automatisch stellen erst ab dieser Abweichung
static const int32_t CLOCK_SYNC_THRESHOLD_S = 60;
static const uint8_t ADDR_PANEL = 0x04;
static const uint8_t ADDR_FAN_UNIT = 0x08;
// Sommerlüftung: Bereiche und Schritt (0,5) wie am Bedienteil (2026-11-26)
static const float SUMMER_THRESHOLD_MIN = 20.0f;   // t_som
static const float SUMMER_THRESHOLD_MAX = 30.0f;
static const float SUMMER_HYSTERESIS_MIN = 0.5f;   // H_som
static const float SUMMER_HYSTERESIS_MAX = 18.0f;
static const float SUMMER_MIN_OUTSIDE_MIN = 12.0f;  // inaktiv unter (Außenluft)
static const float SUMMER_MIN_OUTSIDE_MAX = 20.0f;
// Dauer der Stoßlüftung: Bereich und Schritt wie am Bedienteil (2026-11-25)
static const uint8_t BOOST_DURATION_MIN = 15;
static const uint8_t BOOST_DURATION_MAX = 120;
static const uint8_t BOOST_DURATION_STEP = 5;
// 62 00 00 sendet das Bedienteil vor einer Einstellungsänderung (Menü geöffnet?), ohne Quittung
static const uint16_t OBJ_SETTINGS_DONE = 0x0062;
// Anlage an/aus: Befehl des Bedienteils (0x07), Status (0x08). Bestätigung kann wegen der Neuanmeldung dauern.
static const uint16_t OBJ_SYSTEM_POWER = 0x0007;
static const uint32_t SYSTEM_CONFIRM_MS = 20000;
// 9. Bit auf den ersten zwei Bytes jedes Frames (gemessen am 2026-10-31)
static const uint8_t SEND_MARKED_BYTES = 2;
static const uint8_t FRAME_START = 0x01;
static const uint8_t ADDR_REGISTERS = 0x00;
static const uint8_t MODE_MIN = 1;
static const uint8_t MODE_MAX = 6;
// Datum [Wochentag 0 = Mo][Tag][Monat][Jahr 2-stellig] und Uhrzeit [Sekunde][Minute][Stunde],
// vom Master alle 5 s im Zeit-Frame, vom Bedienteil beim Stellen der Uhr (2026-11-25)
static const uint16_t OBJ_DATE = 0x0001;
static const uint16_t OBJ_TIME = 0x0002;
// Zeit-Frame älter als das: Uhr der Anlage gilt als unbekannt
static const uint32_t PANEL_CLOCK_MAX_AGE_MS = 30000;
// Leistung Stufe 1 und 3: Bereich und Schritt wie am Bedienteil (Stufe 2 fest 60 %)
static const uint8_t STAGE_PERCENT_MIN = 17;
static const uint8_t STAGE_PERCENT_MAX = 100;
// Frostschutz-Art: 0 = eco, 1 = sicher, 2 = Feuchte-WT
static const uint16_t OBJ_FROST_MODE = 0x007B;
static const uint8_t FROST_MODE_MAX = 2;
// Einstellwerte im zusätzlichen Speicher (Länge je ID aus OBJECT_LENGTHS, zusammen höchstens 8 Byte, 32 IDs)
// Frostschutzschwelle minimale Zulufttemperatur, 0,1 °C (Werkseinstellung 5,0 °C)
static const uint16_t OBJ_FROST_MIN_SUPPLY = 0x0047;
// Filter-Intervall am Bedienteil: 30..180 Tage in 10er-Schritten
// Frostschutzschwellen am Bedienteil (Setup): Außenluft -12..10 °C, minimale Zuluft 4..10 °C, je 0,5er-Schritte
static const float FROST_THRESHOLD_MIN = -12.0f;
static const float FROST_THRESHOLD_MAX = 10.0f;
static const float FROST_MIN_SUPPLY_MIN = 4.0f;
static const float FROST_MIN_SUPPLY_MAX = 10.0f;
static const uint16_t FILTER_INTERVAL_MIN = 30;
static const uint16_t FILTER_INTERVAL_MAX = 180;
static const uint16_t FILTER_INTERVAL_STEP = 10;
// Nur hinten anfügen: der Speicherschlüssel bleibt gleich, bestehende Einträge behalten ihren Platz
static const uint16_t EXTRA_PERSISTED_IDS[] = {OBJ_FROST_MODE, OBJ_FROST_MIN_SUPPLY};
static const uint8_t NUM_EXTRA_PERSISTED = sizeof(EXTRA_PERSISTED_IDS) / sizeof(EXTRA_PERSISTED_IDS[0]);
static const char *const WEEKDAYS[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};

static const char *frost_mode_to_string(uint8_t mode) {
  switch (mode) {
    case 0:
      return "eco";
    case 1:
      return "sicher";
    case 2:
      return "Feuchte-WT";
    default:
      return nullptr;
  }
}

void IRAM_ATTR EdgeStore::gpio_intr(EdgeStore *store) {
  const uint32_t time = micros();
  const bool level = store->pin.digital_read();
  const uint32_t next = (store->head + 1) & (EDGE_BUFFER_SIZE - 1);
  if (next == store->tail) {
    store->overflows = store->overflows + 1;
    return;
  }
  store->edges[store->head] = (time & ~1u) | (level ? 1u : 0u);
  store->head = next;
}

void Climos::setup_bit_capture_() {
  if (bit_capture_pin_ == nullptr)
    return;
  edge_store_.edges = new uint32_t[EDGE_BUFFER_SIZE];
  edge_store_.pin = bit_capture_pin_->to_isr();
  // Kein pin->setup(): der Pin gehört dem UART und bleibt so eingestellt, wie der ihn konfiguriert hat
  bit_capture_pin_->attach_interrupt(EdgeStore::gpio_intr, &edge_store_, gpio::INTERRUPT_ANY_EDGE);
  bit_capture_active_ = true;
  last_bit_capture_log_ = millis();
  ESP_LOGI(TAG, "Messmodus 9. Bit gestartet");
}

void Climos::loop_bit_capture_() {
  if (!bit_capture_active_)
    return;

  // Zeit vor dem Leeren lesen: alle Pegelwechsel bis zu diesem Zeitpunkt sind dann im Dekoder
  const uint32_t now = micros();
  uint32_t tail = edge_store_.tail;
  const uint32_t head = edge_store_.head;
  while (tail != head) {
    const uint32_t edge = edge_store_.edges[tail];
    bit_decoder_.add_edge(edge & ~1u, edge & 1u);
    tail = (tail + 1) & (EDGE_BUFFER_SIZE - 1);
  }
  edge_store_.tail = tail;
  bit_decoder_.process(now);

  const bool finished = bit_decoder_.frames_ok() >= BIT_CAPTURE_MAX_FRAMES;
  if (finished || millis() - last_bit_capture_log_ > BIT_CAPTURE_LOG_INTERVAL_MS) {
    last_bit_capture_log_ = millis();
    log_bit_capture_();
  }
  if (finished) {
    bit_capture_pin_->detach_interrupt();
    bit_capture_active_ = false;
    ESP_LOGI(TAG, "Messmodus 9. Bit beendet");
  }
}

void Climos::log_bit_capture_() {
  const auto &d = bit_decoder_;
  ESP_LOGI(TAG, "9. Bit: %u Frames OK, %u CRC-Fehler, %u Stoppbit-Fehler, %u Pufferüberläufe",
           static_cast<unsigned int>(d.frames_ok()), static_cast<unsigned int>(d.crc_errors()),
           static_cast<unsigned int>(d.stop_errors()), static_cast<unsigned int>(edge_store_.overflows));
  ESP_LOGI(TAG, "9. Bit: erstes Byte %u gesetzt / %u nicht, restliche Bytes %u gesetzt / %u nicht",
           static_cast<unsigned int>(d.first_marked()), static_cast<unsigned int>(d.first_unmarked()),
           static_cast<unsigned int>(d.other_marked()), static_cast<unsigned int>(d.other_unmarked()));
  const auto log_gap = [](const char *name, const BitDecoder::Stat &stat) {
    if (stat.count == 0)
      return;
    ESP_LOGI(TAG, "Abstand %s: min %d, mittel %d, max %d µs (%u)", name, static_cast<int>(stat.min),
             static_cast<int>(stat.sum / stat.count), static_cast<int>(stat.max), static_cast<unsigned int>(stat.count));
  };
  log_gap("nach Poll", d.gap_after_poll());
  log_gap("nach 0x86", d.gap_after_confirm());
  log_gap("nach Broadcast-Poll", d.gap_after_broadcast());
  log_gap("nach sonstigem Frame", d.gap_other());
  log_gap("zwischen Bytes im Frame", d.gap_inside_frame());
  for (const auto &entry : d.patterns()) {
    ESP_LOGI(TAG, "%s: %u mal, z.B. %s", entry.first.c_str(), static_cast<unsigned int>(entry.second.count),
             entry.second.example.c_str());
  }
}

void Climos::setup() {
  // Zuerst, damit der Sendepin so früh wie möglich fest auf High liegt
  if (tx_pin_ >= 0) {
    if (sender_.setup(tx_pin_)) {
      ESP_LOGI(TAG, "Sender bereit auf GPIO%d, Ruhepegel High, Schreiben gesperrt", tx_pin_);
    } else {
      ESP_LOGE(TAG, "Sender auf GPIO%d nicht verfügbar", tx_pin_);
    }
  }
  setup_bit_capture_();
  rx_buffer_.reserve(256);
  last_byte_time_ = millis();
  last_stats_time_ = millis();
  setup_extra_settings_();

  size_t offset = 0;
  for (uint8_t i = 0; i < NUM_PERSISTED; i++) {
    settings_offsets_[i] = offset;
    offset += object_length_(PERSISTED_IDS[i]);
  }
  if (offset > SETTINGS_STORE_SIZE) {
    ESP_LOGE(TAG, "Speicher für Einstellwerte zu klein (%u > %u)", static_cast<unsigned int>(offset),
             static_cast<unsigned int>(SETTINGS_STORE_SIZE));
    persistence_ok_ = false;
    return;
  }

  // Schlüssel hängt von der Tabelle ab, damit ein geändertes Layout nicht falsch gelesen wird
  const uint32_t hash = fnv1_hash("climos_settings") ^ (offset << 16) ^ NUM_PERSISTED;
  settings_pref_ = global_preferences->make_preference<SettingsStore>(hash);
  if (!settings_pref_.load(&settings_)) {
    settings_ = SettingsStore{};
    ESP_LOGI(TAG, "Keine gespeicherten Einstellwerte");
    return;
  }
  uint8_t restored = 0;
  for (uint8_t i = 0; i < NUM_PERSISTED; i++) {
    if (settings_.valid & (1ull << i)) {
      handle_object_(PERSISTED_IDS[i], settings_.data + settings_offsets_[i], object_length_(PERSISTED_IDS[i]));
      restored++;
    }
  }
  ESP_LOGI(TAG, "%u gespeicherte Einstellwerte wiederhergestellt", restored);
}

uint8_t Climos::object_length_(uint16_t id) {
  for (const auto &entry : OBJECT_LENGTHS) {
    if (entry.id == id)
      return entry.length;
  }
  return 0;
}

void Climos::setup_extra_settings_() {
  // Fester Schlüssel (Stand mit einer ID); neue IDs nur hinten anfügen, Gültig-Bits halten die Zuordnung
  const uint32_t hash = fnv1_hash("climos_settings_extra") ^ 1u;
  extra_pref_ = global_preferences->make_preference<ExtraStore>(hash);
  if (!extra_pref_.load(&extra_)) {
    extra_ = ExtraStore{};
    return;
  }
  size_t offset = 0;
  for (uint8_t i = 0; i < NUM_EXTRA_PERSISTED; i++) {
    const uint8_t length = object_length_(EXTRA_PERSISTED_IDS[i]);
    if (extra_.valid & (1u << i))
      handle_object_(EXTRA_PERSISTED_IDS[i], extra_.data + offset, length);
    offset += length;
  }
}

bool Climos::remember_extra_setting_(uint16_t id, const uint8_t *value, uint8_t length) {
  size_t offset = 0;
  for (uint8_t i = 0; i < NUM_EXTRA_PERSISTED; i++) {
    const uint8_t stored_length = object_length_(EXTRA_PERSISTED_IDS[i]);
    if (EXTRA_PERSISTED_IDS[i] != id) {
      offset += stored_length;
      continue;
    }
    if (offset + length > sizeof(extra_.data))
      return true;
    const uint32_t bit = 1u << i;
    if ((extra_.valid & bit) && std::memcmp(extra_.data + offset, value, length) == 0)
      return true;
    std::memcpy(extra_.data + offset, value, length);
    extra_.valid |= bit;
    // Ändert sich selten: sofort speichern (ESPHome schreibt den Flash ohnehin verzögert)
    extra_pref_.save(&extra_);
    return true;
  }
  return false;
}

void Climos::remember_setting_(uint16_t id, const uint8_t *value, uint8_t length) {
  if (remember_extra_setting_(id, value, length))
    return;
  if (!persistence_ok_)
    return;
  for (uint8_t i = 0; i < NUM_PERSISTED; i++) {
    if (PERSISTED_IDS[i] != id)
      continue;
    uint8_t *stored = settings_.data + settings_offsets_[i];
    const uint64_t bit = 1ull << i;
    if ((settings_.valid & bit) && std::memcmp(stored, value, length) == 0)
      return;
    std::memcpy(stored, value, length);
    settings_.valid |= bit;
    settings_dirty_ = true;
    return;
  }
}

void Climos::dump_config() {
  ESP_LOGCONFIG(TAG, "Climos Bus-Sniffer");
  LOG_PIN("  Messmodus 9. Bit: ", bit_capture_pin_);
  if (tx_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  Sender: GPIO%d, %s, Schreiben %s", tx_pin_, sender_.is_ready() ? "bereit" : "FEHLER",
                  write_enabled_ ? "erlaubt" : "gesperrt");
  } else {
    ESP_LOGCONFIG(TAG, "  Sender: nicht konfiguriert");
  }
  for (uint8_t i = 0; i < NUM_TEMPERATURES; i++) {
    LOG_SENSOR("  ", "Temperatur", temperature_sensors_[i]);
  }
  LOG_TEXT_SENSOR("  ", "Lüfterstufe", fan_stage_text_sensor_);
  LOG_SENSOR("  ", "Filter-Restlaufzeit", filter_remaining_sensor_);
  LOG_SENSOR("  ", "Betriebsstunden", operating_hours_sensor_);
  LOG_SENSOR("  ", "Betriebsstunden Lüfter", fan_operating_hours_sensor_);
  LOG_SENSOR("  ", "Status 0x1A", status_1a_sensor_);
  LOG_SENSOR("  ", "Filterintervall", filter_interval_sensor_);
  LOG_TEXT_SENSOR("  ", "Betriebsart", operating_mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Letzte Meldung", last_message_text_sensor_);
  LOG_TEXT_SENSOR("  ", "BUS-Version", bus_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "SW Master", sw_master_text_sensor_);
  LOG_TEXT_SENSOR("  ", "SW Slave", sw_slave_text_sensor_);
  LOG_TEXT_SENSOR("  ", "SW Bedienteil", sw_tft_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Frostschutz-Art", frost_mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Uhr der Anlage", panel_clock_text_sensor_);
  LOG_BINARY_SENSOR("  ", "Anlage an", system_on_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Sommerlüftung aktiv", summer_ventilation_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Fortluftventilator", exhaust_fan_binary_sensor_);
  LOG_SENSOR("  ", "Schwelle Sommerlüftung", summer_threshold_sensor_);
  LOG_SENSOR("  ", "Min. Außenluft Sommerlüftung", summer_min_outside_sensor_);
  LOG_SENSOR("  ", "Rohwert 0x44", fan_level_raw_sensor_);
  LOG_SENSOR("  ", "Hysterese Sommerlüftung", summer_hysteresis_sensor_);
  LOG_SENSOR("  ", "Fehlercode", error_code_sensor_);
  for (uint8_t i = 0; i < NUM_FAN_STAGES; i++) {
    LOG_SENSOR("  ", "Stufe in %", stage_percent_sensors_[i]);
  }
  LOG_SENSOR("  ", "Dauer Stoßlüftung", boost_duration_sensor_);
  LOG_SENSOR("  ", "Laufzeit Abwesend", away_interval_sensor_);
  for (uint8_t i = 0; i < NUM_FAN_STAGES; i++) {
    LOG_SENSOR("  ", "Disbalance", debalance_sensors_[i]);
  }
  for (uint8_t i = 0; i < NUM_FROST_THRESHOLDS; i++) {
    LOG_SENSOR("  ", "Frostschutz-Schwelle", frost_threshold_sensors_[i]);
  }
  LOG_SENSOR("  ", "Frostschutz min. Zuluft", frost_min_supply_sensor_);
  LOG_BINARY_SENSOR("  ", "Sommerlüftung freigegeben", summer_enabled_binary_sensor_);
}

void Climos::loop() {
  loop_bit_capture_();

  while (available()) {
    rx_buffer_.push_back(read());
    rx_bytes_++;
    last_byte_time_ = millis();
  }

  while (try_parse_frame_()) {
  }

  loop_send_();

  // Rest eines kaputten oder abgeschnittenen Frames: ein Byte weiter neu synchronisieren
  if (!rx_buffer_.empty() && millis() - last_byte_time_ > STALE_TIMEOUT_MS) {
    rx_buffer_.erase(rx_buffer_.begin());
    while (try_parse_frame_()) {
    }
    if (millis() - last_byte_time_ > STALE_TIMEOUT_MS * 4) {
      dump_frame_("Verworfen", rx_buffer_.data(), rx_buffer_.size());
      rx_buffer_.clear();
    }
  }

  if (millis() - last_stats_time_ > STATS_INTERVAL_MS) {
    last_stats_time_ = millis();
    ESP_LOGI(TAG, "Statistik: %u Frames OK, %u verworfene Bytes (CRC/Resync)", static_cast<unsigned int>(frames_ok_),
             static_cast<unsigned int>(crc_errors_));
  }
}

// Versucht am Pufferanfang ein Frame zu lesen. true = Puffer hat sich verändert, nochmal versuchen.
bool Climos::try_parse_frame_() {
  if (rx_buffer_.size() < HEADER_SIZE)
    return false;

  const size_t frame_size = HEADER_SIZE + (rx_buffer_[3] & 0x7F);
  if (rx_buffer_.size() < frame_size)
    return false;

  const uint8_t *data = rx_buffer_.data();
  uint16_t crc = crc16_xmodem_(data, 4);
  crc = crc16_xmodem_(data + HEADER_SIZE, frame_size - HEADER_SIZE, crc);
  const uint16_t expected = data[4] | (data[5] << 8);

  if (crc != expected) {
    // Kein gültiges Frame an dieser Stelle: ein Byte verwerfen und neu ansetzen
    crc_errors_++;
    rx_buffer_.erase(rx_buffer_.begin());
    return true;
  }

  frames_ok_++;
  handle_frame_(data, frame_size);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + frame_size);
  return true;
}

void Climos::handle_frame_(const uint8_t *frame, size_t size) {
  const uint8_t dst = frame[1];
  const uint8_t cmd = frame[2];
  const uint8_t *payload = frame + HEADER_SIZE;
  const size_t payload_size = size - HEADER_SIZE;
  frame_first_byte_ = frame[0];

  dump_frame_("Frame", frame, size);

  // Die Quittungsabfragen zu unserem Frame kommen direkt danach, vor dem nächsten Einzel-Poll.
  // Spätere 86 gehören zu anderen Frames und zählen nicht.
  if (ack_track_active_) {
    if (cmd == CMD_CONFIRM_REQUEST)
      ack_track_mask_ |= dst == ADDR_PANEL ? 1 : (dst == ADDR_FAN_UNIT ? 2 : 4);
    if (cmd == CMD_POLL) {
      if (dst != ADDR_BROADCAST)
        ack_track_first_poll_ = dst;
      finish_ack_track_();
    }
  }
  if (cmd == CMD_POLL) {
    if (dst != ADDR_BROADCAST)
      last_unicast_poll_ = dst;
    else
      poll_before_ff_ = last_unicast_poll_;
    last_polled_ = dst;
    if (dst == ADDR_BROADCAST && payload_size == 0) {
      // Bytes, die im Puffer schon hinter diesem Frame stehen, zählen als "nach dem Poll"
      ff_poll_seen_ = true;
      ff_poll_time_ = millis();
      ff_poll_rx_bytes_ = rx_bytes_ - (rx_buffer_.size() - size);
    }
  }

  if (watching_ && static_cast<int32_t>(millis() - watch_until_) > 0) {
    watching_ = false;
    ESP_LOGI(TAG, "Beobachtung nach dem Senden beendet");
  }
  watch_hit_ = false;

  if (cmd == CMD_DATA) {
    parse_records_(payload, payload_size);
    // Ein Komplett-Abzug ändert viele Werte auf einmal: einmal pro Frame speichern
    if (settings_dirty_) {
      settings_pref_.save(&settings_);
      settings_dirty_ = false;
      ESP_LOGD(TAG, "Einstellwerte gespeichert");
    }
  }

  // Reaktion auf das eigene Frame: Betriebsart-Objekte und Frames mit erstem Byte 00 (ohne Uhrzeit)
  if (watching_ && (watch_hit_ || (frame[0] == 0x00 && cmd != CMD_TIME))) {
    std::string hex = to_hex_(frame, size);
    if (hex != last_watch_log_) {
      ESP_LOGI(TAG, "Nach Senden +%u ms (nach Poll %02X): %s", static_cast<unsigned int>(millis() - last_send_time_),
               last_polled_, hex.c_str());
      last_watch_log_ = std::move(hex);
    }
  }

  // Zustellquittung: Master fragt ein Gerät mit 86, das antwortet mit 87 [00][CRC lo][CRC hi]
  // des empfangenen Frames. Für unser Frame zählt nur die Quittung des Lüftergeräts.
  if (cmd == CMD_CONFIRM_REQUEST)
    last_ack_request_dst_ = dst;
  if (cmd == CMD_ACK && payload_size >= 3 && sent_size_ > 0 && payload[1] == sent_frame_[4] &&
      payload[2] == sent_frame_[5]) {
    const uint8_t from = last_ack_request_dst_;
    if (watching_)
      ESP_LOGI(TAG, "Nach Senden +%u ms: Quittung von %02X für unser Frame (%s)",
               static_cast<unsigned int>(millis() - last_send_time_), from, to_hex_(frame, size).c_str());
    if (confirm_kind_ == CONFIRM_ECHO || retry_pending_(CONFIRM_ECHO)) {
      if (from == ADDR_FAN_UNIT)
        confirm_success_(CONFIRM_ECHO, "Quittung des Lüftergeräts (87)");
    } else if (confirm_kind_ == CONFIRM_ACK || retry_pending_(CONFIRM_ACK)) {
      if (from == ADDR_FAN_UNIT) {
        confirm_success_(CONFIRM_ACK, "Quittung des Lüftergeräts (87)");
      } else {
        acked_by_panel_only_ = true;
        // Das Lüftergerät quittiert oft kurz danach selbst; gewarnt wird erst, wenn die Frist abläuft
        ESP_LOGD(TAG, "Quittung von %02X, warte auf das Lüftergerät", from);
      }
    }
  }

  // Uhrzeit (alle 5 s) und Bestätigungen enthalten nichts Neues: nur auf DEBUG.
  // Datum und Uhrzeit aus dem Zeit-Frame für den Sensor auswerten (nicht gespeichert).
  if (cmd == CMD_TIME)
    parse_records_(payload, payload_size);
  if (cmd == CMD_TIME || cmd == CMD_ACK) {
    ESP_LOGD(TAG, "%s", to_hex_(frame, size).c_str());
    return;
  }

  // Objekt 0x0055 (Stellwert 0-10-V-Eingang, 0.1 %) am Payload-Ende rauscht: beim Vergleich ignorieren
  size_t compare_size = payload_size;
  if (payload_size >= 4 && payload[payload_size - 4] == OBJ_UNKNOWN_55 && payload[payload_size - 3] == 0x00) {
    ESP_LOGD(TAG, "Objekt 0x55: %u", payload[payload_size - 2]);
    compare_size -= 4;
  }

  // Nur Änderungen auf INFO loggen. Schlüssel: Kopf ohne CRC und die ersten beiden Payload-Bytes (Objekt-ID).
  char key[24];
  std::snprintf(key, sizeof(key), "%02X%02X%02X%02X:%02X%02X", frame[0], frame[1], frame[2], frame[3],
                payload_size > 0 ? payload[0] : 0, payload_size > 1 ? payload[1] : 0);
  std::vector<uint8_t> current(payload, payload + compare_size);
  auto it = last_payloads_.find(key);
  if (it == last_payloads_.end() || it->second != current) {
    ESP_LOGI(TAG, "%s (nach Poll %02X): %s", it == last_payloads_.end() ? "Neu" : "Geändert", last_polled_,
             to_hex_(frame, size).c_str());
    last_payloads_[key] = std::move(current);
  } else {
    ESP_LOGD(TAG, "%s", to_hex_(frame, size).c_str());
  }
}

void Climos::parse_records_(const uint8_t *payload, size_t size) {
  size_t pos = 0;
  while (pos + 2 <= size) {
    const uint16_t id = payload[pos] | (payload[pos + 1] << 8);
    const uint8_t length = object_length_(id);
    if (length == 0 || pos + 2 + length > size) {
      // Unbekanntes Objekt: Länge unbekannt, Rest der Payload nicht auswertbar
      ESP_LOGD(TAG, "Unbekanntes Objekt 0x%04X an Position %u", id, static_cast<unsigned int>(pos));
      return;
    }
    remember_setting_(id, payload + pos + 2, length);
    handle_object_(id, payload + pos + 2, length);
    pos += 2 + length;
  }
}

void Climos::handle_object_(uint16_t id, const uint8_t *value, uint8_t length) {
  // Beim Senden beobachten: Betriebsart, Freigaben, t_som und Abschluss-Objekt
  if (id == OBJ_MODE || id == OBJ_MODE_FALLBACK || id == OBJ_FEATURES || id == OBJ_SUMMER_THRESHOLD ||
      id == OBJ_BOOST_DURATION || id == OBJ_SETTINGS_DONE || id == OBJ_SYSTEM_POWER || id == OBJ_SYSTEM_ON ||
      id == OBJ_FROST_MODE || (id >= OBJ_FIRST_STAGE_PERCENT && id < OBJ_FIRST_STAGE_PERCENT + NUM_FAN_STAGES))
    watch_hit_ = true;
  // Bestätigung Anlage an/aus: Status 0x08 mit dem neuen Wert oder Echo des Masters (erstes Byte 00) von 0x07
  if (confirm_kind_ == CONFIRM_SYSTEM && (id == OBJ_SYSTEM_ON || (id == OBJ_SYSTEM_POWER && frame_first_byte_ == 0x00))) {
    if ((value[0] != 0) == (confirm_mode_ != 0))
      confirm_success_(CONFIRM_SYSTEM, id == OBJ_SYSTEM_ON ? "Status 0x08" : "Echo des Masters (0x07)");
  }
  if (frame_first_byte_ == 0x00 && (id == OBJ_DATE || id == OBJ_TIME))
    check_echo_confirm_(id, value);
  if (id == OBJ_MODE || id == OBJ_MODE_FALLBACK) {
    // Bestätigung der gesendeten Betriebsart (auch für eine bereits vorgemerkte Wiederholung)
    const uint8_t expected =
        confirm_kind_ == CONFIRM_MODE ? confirm_mode_ : (retry_pending_(CONFIRM_MODE) ? pending_mode_ : 0);
    const bool from_fan = id == OBJ_MODE_FALLBACK && last_polled_ == ADDR_FAN_UNIT;
    const bool from_panel = id == OBJ_MODE && last_polled_ == ADDR_PANEL;
    if (expected != 0 && value[0] == expected && (from_fan || from_panel))
      confirm_success_(CONFIRM_MODE, from_fan ? "Lüftergerät (0x29)" : "Bedienteil (0x28)");
  }
  if (id >= OBJ_FIRST_SCHEDULE && id < OBJ_FIRST_SCHEDULE + NUM_WEEKDAYS * SCHEDULE_REGISTERS_PER_DAY) {
    const uint8_t index = id - OBJ_FIRST_SCHEDULE;
    std::memcpy(schedule_[index], value, SCHEDULE_REGISTER_SIZE);
    schedule_received_ |= 1u << index;
    publish_schedule_day_(index / SCHEDULE_REGISTERS_PER_DAY);
    return;
  }
  if (id >= OBJ_FIRST_DEBALANCE && id < OBJ_FIRST_DEBALANCE + NUM_FAN_STAGES) {
    publish_if_changed_(debalance_sensors_[id - OBJ_FIRST_DEBALANCE], static_cast<int8_t>(value[0]));
    return;
  }
  if (id >= OBJ_FIRST_FROST_THRESHOLD && id < OBJ_FIRST_FROST_THRESHOLD + NUM_FROST_THRESHOLDS) {
    const int16_t raw = static_cast<int16_t>(value[0] | (value[1] << 8));
    publish_if_changed_(frost_threshold_sensors_[id - OBJ_FIRST_FROST_THRESHOLD], raw / 10.0f);
    return;
  }
  if (id >= OBJ_FIRST_STAGE_PERCENT && id < OBJ_FIRST_STAGE_PERCENT + NUM_FAN_STAGES) {
    publish_if_changed_(stage_percent_sensors_[id - OBJ_FIRST_STAGE_PERCENT], value[0]);
    return;
  }
  if (id >= OBJ_FIRST_TEMPERATURE && id < OBJ_FIRST_TEMPERATURE + NUM_TEMPERATURES) {
    const int16_t raw = static_cast<int16_t>(value[0] | (value[1] << 8));
    publish_if_changed_(temperature_sensors_[id - OBJ_FIRST_TEMPERATURE], raw / 10.0f);
    return;
  }

  switch (id) {
    case OBJ_FAN_STAGE:
      ESP_LOGI(TAG, "Lüfterstufe: %u", value[0]);
      // Gleiche Codes wie die Betriebsart; in Automatik steht hier die gefahrene Stufe
      publish_if_changed_(fan_stage_text_sensor_, mode_to_string_(value[0]));
      break;
    case OBJ_STATUS_1A:
      publish_if_changed_(status_1a_sensor_, value[0]);
      publish_if_changed_(summer_ventilation_binary_sensor_, value[0] & STATUS_SUMMER_VENTILATION);
      publish_if_changed_(exhaust_fan_binary_sensor_, value[0] & STATUS_EXHAUST_FAN);
      break;
    case OBJ_SYSTEM_ON:
      publish_if_changed_(system_on_binary_sensor_, value[0] != 0);
      break;
    case OBJ_SUMMER_THRESHOLD:
      publish_if_changed_(summer_threshold_sensor_, static_cast<int16_t>(value[0] | (value[1] << 8)) / 10.0f);
      break;
    case OBJ_SUMMER_MIN_OUTSIDE:
      publish_if_changed_(summer_min_outside_sensor_, static_cast<int16_t>(value[0] | (value[1] << 8)) / 10.0f);
      break;
    case OBJ_FAN_LEVEL_RAW:
      publish_if_changed_(fan_level_raw_sensor_, value[0]);
      break;
    case OBJ_SUMMER_HYSTERESIS:
      publish_if_changed_(summer_hysteresis_sensor_, value[0] / 10.0f);
      break;
    case OBJ_BOOST_DURATION:
      publish_if_changed_(boost_duration_sensor_, value[0]);
      break;
    case OBJ_AWAY_INTERVAL:
      publish_if_changed_(away_interval_sensor_, value[0]);
      break;
    case OBJ_ERROR_CODE:
      publish_if_changed_(error_code_sensor_, value[0]);
      break;
    case OBJ_FROST_MODE: {
      const char *name = frost_mode_to_string(value[0]);
      char buf[16];
      if (name == nullptr) {
        std::snprintf(buf, sizeof(buf), "Code %u", value[0]);
        name = buf;
      }
      publish_if_changed_(frost_mode_text_sensor_, name);
      break;
    }
    case OBJ_DATE:
      std::memcpy(panel_date_, value, sizeof(panel_date_));
      has_panel_date_ = true;
      publish_panel_clock_();
      break;
    case OBJ_TIME:
      std::memcpy(panel_time_, value, sizeof(panel_time_));
      has_panel_time_ = true;
      panel_time_ms_ = millis();
      publish_panel_clock_();
      break;
    case OBJ_FEATURES:
      features_raw_[0] = value[0];
      features_raw_[1] = value[1];
      has_features_ = true;
      publish_if_changed_(summer_enabled_binary_sensor_, value[0] & FEATURE_SUMMER_VENTILATION);
      break;
    case OBJ_OPERATING_TIME:
      publish_if_changed_(operating_hours_sensor_, duration_to_minutes_(value) / 60);
      break;
    case OBJ_FAN_OPERATING_TIME:
      publish_if_changed_(fan_operating_hours_sensor_, duration_to_minutes_(value) / 60);
      break;
    case OBJ_FILTER_REMAINING:
      publish_if_changed_(filter_remaining_sensor_, duration_to_minutes_(value) / (24 * 60));
      break;
    case OBJ_FROST_MIN_SUPPLY:
      publish_if_changed_(frost_min_supply_sensor_, static_cast<int16_t>(value[0] | (value[1] << 8)) / 10.0f);
      break;
    case OBJ_FILTER_INTERVAL:
      filter_interval_days_ = static_cast<uint16_t>(duration_to_minutes_(value) / (24 * 60));
      has_filter_interval_ = true;
      publish_if_changed_(filter_interval_sensor_, duration_to_minutes_(value) / (24 * 60));
      break;
    case OBJ_MODE:
      current_mode_ = value[0];
      publish_if_changed_(operating_mode_text_sensor_, mode_to_string_(value[0]));
      break;
    case OBJ_BUS_VERSION: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%u.%u.%u", value[0], value[1], value[2]);
      publish_if_changed_(bus_version_text_sensor_, buf);
      break;
    }
    case OBJ_SW_MASTER:
      publish_if_changed_(sw_master_text_sensor_, std::string(reinterpret_cast<const char *>(value), length));
      break;
    case OBJ_SW_SLAVE:
      publish_if_changed_(sw_slave_text_sensor_, std::string(reinterpret_cast<const char *>(value), length));
      break;
    case OBJ_SW_TFT:
      publish_if_changed_(sw_tft_text_sensor_, std::string(reinterpret_cast<const char *>(value), length));
      break;
    // Meldung 1 kommt als Code, Datum, Uhrzeit hintereinander im selben Frame
    case OBJ_MESSAGE_CODE:
      message_code_ = value[0];
      break;
    case OBJ_MESSAGE_DATE:
      std::memcpy(message_date_, value, sizeof(message_date_));
      break;
    case OBJ_MESSAGE_TIME: {
      // Datum: [Wochentag][Tag][Monat][Jahr], Uhrzeit: [?][Minute][Stunde]
      const char *text = message_code_ == MESSAGE_OUTSIDE_TEMP_LOW ? "Außentemperatur zu niedrig" : nullptr;
      char buf[80];
      if (text != nullptr) {
        std::snprintf(buf, sizeof(buf), "%02u.%02u.%02u %02u:%02u %s", message_date_[1], message_date_[2],
                      message_date_[3], value[2], value[1], text);
      } else {
        std::snprintf(buf, sizeof(buf), "%02u.%02u.%02u %02u:%02u Code 0x%02X", message_date_[1], message_date_[2],
                      message_date_[3], value[2], value[1], message_code_);
      }
      publish_if_changed_(last_message_text_sensor_, buf);
      break;
    }
    default:
      break;
  }
}

// Uhr der Anlage, z.B. "Mi 25.11.2026 20:34" (nur Minuten, damit der Sensor nicht alle 5 s sendet)
void Climos::publish_panel_clock_() {
  if (!has_panel_date_ || !has_panel_time_)
    return;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s %02u.%02u.%04u %02u:%02u", panel_date_[0] < 7 ? WEEKDAYS[panel_date_[0]] : "?",
                panel_date_[1], panel_date_[2], 2000u + panel_date_[3], panel_time_[2], panel_time_[1]);
  publish_if_changed_(panel_clock_text_sensor_, buf);
}

int32_t Climos::panel_clock_deviation(const ESPTime &now) const {
  if (!now.is_valid() || !has_panel_date_ || !has_panel_time_ || millis() - panel_time_ms_ > PANEL_CLOCK_MAX_AGE_MS)
    return INT32_MAX;
  if (panel_date_[1] != now.day_of_month || panel_date_[2] != now.month || 2000 + panel_date_[3] != now.year)
    return 86400;
  const int32_t panel = panel_time_[2] * 3600 + panel_time_[1] * 60 + panel_time_[0] +
                        static_cast<int32_t>((millis() - panel_time_ms_) / 1000);
  const int32_t ours = now.hour * 3600 + now.minute * 60 + now.second;
  return panel - ours;
}

// Tag als Liste der Umschaltpunkte, z.B. "00:00 Stufe 1, 07:00 Stufe 2, 19:30 Stufe 1"
void Climos::publish_schedule_day_(uint8_t day) {
  const uint32_t day_mask = 0b111u << (day * SCHEDULE_REGISTERS_PER_DAY);
  if (schedule_text_sensors_[day] == nullptr || (schedule_received_ & day_mask) != day_mask)
    return;

  std::string text;
  int last_level = -1;
  for (uint8_t slot = 0; slot < 96; slot++) {
    const uint8_t byte = schedule_[day * SCHEDULE_REGISTERS_PER_DAY + slot / 32][(slot % 32) / 4];
    const int level = (byte >> ((slot % 4) * 2)) & 0x03;
    if (level == last_level)
      continue;
    last_level = level;
    char buf[24];
    if (level == 0) {
      std::snprintf(buf, sizeof(buf), "%s%02u:%02u aus", text.empty() ? "" : ", ", slot / 4, (slot % 4) * 15);
    } else {
      std::snprintf(buf, sizeof(buf), "%s%02u:%02u Stufe %d", text.empty() ? "" : ", ", slot / 4, (slot % 4) * 15,
                    level);
    }
    if (text.size() + std::strlen(buf) > MAX_SCHEDULE_TEXT) {
      text += " …";
      break;
    }
    text += buf;
  }
  publish_if_changed_(schedule_text_sensors_[day], text);
}

// Zeitdauer im 5-Byte-Format: [Minuten][Stunden][Tage lo][Tage hi][Jahre], Jahr = 365 Tage
uint32_t Climos::duration_to_minutes_(const uint8_t *value) {
  const uint32_t days = (value[2] | (value[3] << 8)) + value[4] * 365u;
  return value[0] + value[1] * 60u + days * 24u * 60u;
}

std::string Climos::mode_to_string_(uint8_t mode) {
  switch (mode) {
    case 1:
      return "Stufe 1";
    case 2:
      return "Stufe 2";
    case 3:
      return "Stufe 3";
    case 4:
      return "Stoßlüftung";
    case 5:
      return "Abwesend";
    case 6:
      return "Automatik";
    default: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "Code %u", mode);
      return buf;
    }
  }
}

void Climos::publish_if_changed_(binary_sensor::BinarySensor *sens, bool value) {
  if (sens != nullptr && (!sens->has_state() || sens->state != value)) {
    sens->publish_state(value);
  }
}

void Climos::publish_if_changed_(text_sensor::TextSensor *sens, const std::string &value) {
  if (sens != nullptr && (!sens->has_state() || sens->state != value)) {
    sens->publish_state(value);
  }
}

void Climos::publish_if_changed_(sensor::Sensor *sens, float value) {
  if (sens != nullptr && (!sens->has_state() || sens->state != value)) {
    sens->publish_state(value);
  }
}

void Climos::dump_frame_(const char *prefix, const uint8_t *data, size_t size) {
  if (size == 0)
    return;
  ESP_LOGV(TAG, "%s (%u Bytes): %s", prefix, static_cast<unsigned int>(size), to_hex_(data, size).c_str());
}

std::string Climos::to_hex_(const uint8_t *data, size_t size) {
  std::string hex;
  char buf[4];
  for (size_t i = 0; i < size; i++) {
    std::snprintf(buf, sizeof(buf), "%02X ", data[i]);
    hex += buf;
  }
  return hex;
}

void Climos::set_write_enabled(bool enabled) {
  if (enabled == write_enabled_)
    return;
  write_enabled_ = enabled;
  ESP_LOGW(TAG, "Senden %s", enabled ? "aktiv" : "GESPERRT");
  if (!enabled && !queue_.empty()) {
    ESP_LOGW(TAG, "Warteschlange geleert (%u Einträge)", static_cast<unsigned int>(queue_.size()));
    queue_.clear();
  }
}

bool Climos::send_operating_mode(uint8_t mode) {
  if (mode < MODE_MIN || mode > MODE_MAX) {
    ESP_LOGW(TAG, "Betriebsart %u ungültig, erlaubt sind %u..%u", mode, MODE_MIN, MODE_MAX);
    return false;
  }
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_MODE & 0xFF), static_cast<uint8_t>(OBJ_MODE >> 8), mode};
  return queue_request_(payload, sizeof(payload), CONFIRM_MODE, mode, mode_to_string_(mode));
}

bool Climos::send_current_operating_mode() {
  if (current_mode_ == 0) {
    ESP_LOGW(TAG, "Betriebsart noch unbekannt, nichts gesendet");
    return false;
  }
  return send_operating_mode(current_mode_);
}

// Halbe Grad/Kelvin wie am Bedienteil: Wert in 0,1 als ganze Zahl, nur Vielfache von 5
static bool half_step_value(float value, float min, float max, int16_t &raw) {
  if (std::isnan(value) || value < min - 0.01f || value > max + 0.01f)
    return false;
  const long tenths = std::lround(value * 10.0f);
  if (tenths % 5 != 0)
    return false;
  raw = static_cast<int16_t>(tenths);
  return true;
}

bool Climos::send_summer_threshold(float celsius) {
  int16_t raw;
  if (!half_step_value(celsius, SUMMER_THRESHOLD_MIN, SUMMER_THRESHOLD_MAX, raw)) {
    ESP_LOGW(TAG, "t_som %.1f °C ungültig, erlaubt sind %.1f..%.1f °C in 0,5er-Schritten", celsius,
             SUMMER_THRESHOLD_MIN, SUMMER_THRESHOLD_MAX);
    return false;
  }
  char label[32];
  std::snprintf(label, sizeof(label), "t_som %.1f °C", raw / 10.0f);
  // Wie das Bedienteil im Menü Sommerlüftung (2026-11-26): 0x41 allein, ohne 0x1B
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_SUMMER_THRESHOLD & 0xFF),
                             static_cast<uint8_t>(OBJ_SUMMER_THRESHOLD >> 8), static_cast<uint8_t>(raw & 0xFF),
                             static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_frost_threshold(uint8_t mode, float celsius) {
  // Register 0x4A = sicher, 0x4B = eco, 0x4C = Feuchte-WT (am Bedienteil bestätigt)
  static const uint16_t IDS[] = {OBJ_FIRST_FROST_THRESHOLD + 1, OBJ_FIRST_FROST_THRESHOLD, OBJ_FIRST_FROST_THRESHOLD + 2};
  if (mode > FROST_MODE_MAX) {
    ESP_LOGW(TAG, "Frostschutz-Art %u ungültig", mode);
    return false;
  }
  int16_t raw;
  if (!half_step_value(celsius, FROST_THRESHOLD_MIN, FROST_THRESHOLD_MAX, raw)) {
    ESP_LOGW(TAG, "Frostschutzschwelle %.1f °C ungültig, erlaubt sind %.1f..%.1f °C in 0,5er-Schritten", celsius,
             FROST_THRESHOLD_MIN, FROST_THRESHOLD_MAX);
    return false;
  }
  char label[48];
  std::snprintf(label, sizeof(label), "Frostschutzschwelle %s %.1f °C", frost_mode_to_string(mode), raw / 10.0f);
  const uint16_t id = IDS[mode];
  const uint8_t payload[] = {static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8),
                             static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_frost_min_supply(float celsius) {
  int16_t raw;
  if (!half_step_value(celsius, FROST_MIN_SUPPLY_MIN, FROST_MIN_SUPPLY_MAX, raw)) {
    ESP_LOGW(TAG, "Frostschutz min. Zuluft %.1f °C ungültig, erlaubt sind %.1f..%.1f °C in 0,5er-Schritten", celsius,
             FROST_MIN_SUPPLY_MIN, FROST_MIN_SUPPLY_MAX);
    return false;
  }
  char label[48];
  std::snprintf(label, sizeof(label), "Frostschutz min. Zuluft %.1f °C", raw / 10.0f);
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_FROST_MIN_SUPPLY & 0xFF),
                             static_cast<uint8_t>(OBJ_FROST_MIN_SUPPLY >> 8), static_cast<uint8_t>(raw & 0xFF),
                             static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_summer_hysteresis(float kelvin) {
  int16_t raw;
  if (!half_step_value(kelvin, SUMMER_HYSTERESIS_MIN, SUMMER_HYSTERESIS_MAX, raw)) {
    ESP_LOGW(TAG, "H_som %.1f K ungültig, erlaubt sind %.1f..%.1f K in 0,5er-Schritten", kelvin, SUMMER_HYSTERESIS_MIN,
             SUMMER_HYSTERESIS_MAX);
    return false;
  }
  char label[32];
  std::snprintf(label, sizeof(label), "H_som %.1f K", raw / 10.0f);
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_SUMMER_HYSTERESIS & 0xFF),
                             static_cast<uint8_t>(OBJ_SUMMER_HYSTERESIS >> 8), static_cast<uint8_t>(raw)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_summer_min_outside(float celsius) {
  int16_t raw;
  if (!half_step_value(celsius, SUMMER_MIN_OUTSIDE_MIN, SUMMER_MIN_OUTSIDE_MAX, raw)) {
    ESP_LOGW(TAG, "Sommerlüftung inaktiv unter %.1f °C ungültig, erlaubt sind %.1f..%.1f °C in 0,5er-Schritten",
             celsius, SUMMER_MIN_OUTSIDE_MIN, SUMMER_MIN_OUTSIDE_MAX);
    return false;
  }
  char label[40];
  std::snprintf(label, sizeof(label), "Sommer inaktiv unter %.1f °C", raw / 10.0f);
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_SUMMER_MIN_OUTSIDE & 0xFF),
                             static_cast<uint8_t>(OBJ_SUMMER_MIN_OUTSIDE >> 8), static_cast<uint8_t>(raw & 0xFF),
                             static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_boost_duration(float minutes) {
  const long value = std::isnan(minutes) ? -1 : std::lround(minutes);
  if (value < BOOST_DURATION_MIN || value > BOOST_DURATION_MAX || value % BOOST_DURATION_STEP != 0) {
    ESP_LOGW(TAG, "Dauer Stoßlüftung %.0f min ungültig, erlaubt sind %u..%u in %u-er Schritten", minutes,
             BOOST_DURATION_MIN, BOOST_DURATION_MAX, BOOST_DURATION_STEP);
    return false;
  }
  char label[32];
  std::snprintf(label, sizeof(label), "Stoßlüftung %ld min", value);
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_BOOST_DURATION & 0xFF),
                             static_cast<uint8_t>(OBJ_BOOST_DURATION >> 8), static_cast<uint8_t>(value)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_stage_percent(uint8_t stage, float percent) {
  const long value = std::isnan(percent) ? -1 : std::lround(percent);
  if (stage != 1 && stage != 3) {
    ESP_LOGW(TAG, "Stufe %u nicht einstellbar (nur 1 und 3, Stufe 2 ist fest)", stage);
    return false;
  }
  if (value < STAGE_PERCENT_MIN || value > STAGE_PERCENT_MAX) {
    ESP_LOGW(TAG, "Leistung Stufe %u: %.0f %% ungültig, erlaubt sind %u..%u %%", stage, percent, STAGE_PERCENT_MIN,
             STAGE_PERCENT_MAX);
    return false;
  }
  const uint16_t id = OBJ_FIRST_STAGE_PERCENT + stage - 1;
  char label[32];
  std::snprintf(label, sizeof(label), "Stufe %u %ld %%", stage, value);
  const uint8_t payload[] = {static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8),
                             static_cast<uint8_t>(value)};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_away_interval(uint8_t minutes) {
  if (minutes != 15 && minutes != 30 && minutes != 45) {
    ESP_LOGW(TAG, "Laufzeit Abwesend %u min ungültig, erlaubt sind 15, 30, 45", minutes);
    return false;
  }
  char label[32];
  std::snprintf(label, sizeof(label), "Abwesend %u min/h", minutes);
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_AWAY_INTERVAL & 0xFF), static_cast<uint8_t>(OBJ_AWAY_INTERVAL >> 8),
                             minutes};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_filter_interval(float days) {
  const long value = std::isnan(days) ? -1 : std::lround(days);
  if (value < FILTER_INTERVAL_MIN || value > FILTER_INTERVAL_MAX || value % FILTER_INTERVAL_STEP != 0) {
    ESP_LOGW(TAG, "Filter-Intervall %.0f Tage ungültig, erlaubt sind %u..%u in %u-er Schritten", days,
             FILTER_INTERVAL_MIN, FILTER_INTERVAL_MAX, FILTER_INTERVAL_STEP);
    return false;
  }
  char label[32];
  std::snprintf(label, sizeof(label), "Filter-Intervall %ld Tage", value);
  // Zeitdauer [Minuten][Stunden][Tage lo][Tage hi][Jahre]
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_FILTER_INTERVAL & 0xFF), static_cast<uint8_t>(OBJ_FILTER_INTERVAL >> 8),
                             0, 0, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8), 0};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_filter_reset() {
  if (!has_filter_interval_ || filter_interval_days_ < FILTER_INTERVAL_MIN || filter_interval_days_ > FILTER_INTERVAL_MAX) {
    ESP_LOGW(TAG, "Filter-Reset nicht gesendet: Filter-Intervall unbekannt");
    publish_send_status_("nicht möglich (Filter-Intervall unbekannt): Filter-Reset");
    return false;
  }
  const uint16_t d = filter_interval_days_;
  char label[40];
  std::snprintf(label, sizeof(label), "Filter-Reset auf %u Tage", d);
  // Wie das Bedienteil (2026-11-25): Intervall und Restlaufzeit in einem Frame, beide = Intervall
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_FILTER_INTERVAL & 0xFF),
                             static_cast<uint8_t>(OBJ_FILTER_INTERVAL >> 8),
                             0, 0, static_cast<uint8_t>(d & 0xFF), static_cast<uint8_t>(d >> 8), 0,
                             static_cast<uint8_t>(OBJ_FILTER_REMAINING & 0xFF),
                             static_cast<uint8_t>(OBJ_FILTER_REMAINING >> 8),
                             0, 0, static_cast<uint8_t>(d & 0xFF), static_cast<uint8_t>(d >> 8), 0};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_frost_mode(uint8_t mode) {
  if (mode > FROST_MODE_MAX) {
    ESP_LOGW(TAG, "Frostschutz-Art %u ungültig, erlaubt sind 0..%u", mode, FROST_MODE_MAX);
    return false;
  }
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_FROST_MODE & 0xFF), static_cast<uint8_t>(OBJ_FROST_MODE >> 8),
                             mode};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0,
                        std::string("Frostschutz ") + frost_mode_to_string(mode));
}

bool Climos::send_summer_enabled(bool enabled) {
  const char *label = enabled ? "Sommerlüftung freigegeben" : "Sommerlüftung gesperrt";
  if (!has_features_) {
    // Die übrigen Bits von 0x1B müssen erhalten bleiben
    ESP_LOGW(TAG, "%s nicht gesendet: Wert von 0x1B unbekannt (kommt erst mit dem Komplett-Abzug)", label);
    publish_send_status_((std::string("nicht möglich (0x1B unbekannt): ") + label).c_str());
    return false;
  }
  const uint8_t low = enabled ? (features_raw_[0] | FEATURE_SUMMER_VENTILATION)
                              : (features_raw_[0] & static_cast<uint8_t>(~FEATURE_SUMMER_VENTILATION));
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_FEATURES & 0xFF), static_cast<uint8_t>(OBJ_FEATURES >> 8), low,
                             features_raw_[1]};
  return queue_request_(payload, sizeof(payload), CONFIRM_ACK, 0, label);
}

bool Climos::send_clock(const ESPTime &now) {
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Uhr nicht gestellt: keine gültige Uhrzeit (NTP?)");
    publish_send_status_("nicht möglich (keine Uhrzeit): Uhr stellen");
    return false;
  }
  const uint32_t seconds = now.hour * 3600u + now.minute * 60u + now.second;
  // Datum und Uhrzeit gehen in zwei Frames: kurz vor Mitternacht könnte das Datum nicht mehr passen
  if (seconds > 86400u - 120u) {
    ESP_LOGW(TAG, "Uhr nicht gestellt: zu nah an Mitternacht");
    publish_send_status_("nicht möglich (Mitternacht): Uhr stellen");
    return false;
  }
  // ESPTime: 1 = Sonntag .. 7 = Samstag; Master: 0 = Montag .. 6 = Sonntag. Das Bedienteil schickte am
  // Mittwoch aber 03, der Master wiederholte 02 (2026-11-25). Wie das Bedienteil senden (vermutlich 0 = Sonntag),
  // das passt, ob der Master den Wochentag umrechnet oder aus dem Datum bestimmt.
  const uint8_t weekday = (now.day_of_week + 5) % 7;
  const uint8_t panel_weekday = now.day_of_week - 1;
  QueuedRequest req;
  req.frames = {
      {static_cast<uint8_t>(OBJ_DATE & 0xFF), static_cast<uint8_t>(OBJ_DATE >> 8), panel_weekday, now.day_of_month,
       now.month, static_cast<uint8_t>(now.year % 100)},
      {static_cast<uint8_t>(OBJ_TIME & 0xFF), static_cast<uint8_t>(OBJ_TIME >> 8), now.second, now.minute, now.hour},
  };
  char label[48];
  std::snprintf(label, sizeof(label), "Uhr %s %02u.%02u.%04u %02u:%02u", WEEKDAYS[weekday], now.day_of_month,
                now.month, now.year, now.hour, now.minute);
  req.key = OBJ_DATE;
  req.kind = CONFIRM_ECHO;
  req.label = label;
  // Bezugszeit jetzt: wartet die Anforderung, wird die Uhrzeit beim Senden entsprechend weitergezählt
  req.clock = true;
  req.clock_base_seconds = seconds;
  req.clock_base_ms = millis();
  return enqueue_(std::move(req));
}

bool Climos::sync_clock_if_needed(const ESPTime &now) {
  const int32_t deviation = panel_clock_deviation(now);
  if (deviation == INT32_MAX || std::abs(deviation) <= CLOCK_SYNC_THRESHOLD_S)
    return false;
  if (is_busy()) {
    ESP_LOGI(TAG, "Uhr der Anlage weicht %d s ab, nicht gestellt: andere Anforderung läuft", static_cast<int>(deviation));
    return false;
  }
  ESP_LOGI(TAG, "Uhr der Anlage weicht %d s ab, wird gestellt", static_cast<int>(deviation));
  return send_clock(now);
}

// Datum/Uhrzeit bestätigt, wenn der Master (erstes Byte 00) den gesendeten Wert meldet: Datum ohne Wochentag
// (den setzt der Master selbst), Uhrzeit auf CLOCK_CONFIRM_TOLERANCE_S genau
void Climos::check_echo_confirm_(uint16_t id, const uint8_t *value) {
  if (!(confirm_kind_ == CONFIRM_ECHO || retry_pending_(CONFIRM_ECHO)) || sent_size_ < HEADER_SIZE + 2)
    return;
  const uint8_t *sent = sent_frame_ + HEADER_SIZE;
  if ((sent[0] | (sent[1] << 8)) != id)
    return;
  if (id == OBJ_DATE) {
    if (sent_size_ < HEADER_SIZE + 6 || std::memcmp(value + 1, sent + 3, 3) != 0)
      return;
    confirm_success_(CONFIRM_ECHO, "Datum vom Master übernommen");
    return;
  }
  if (sent_size_ < HEADER_SIZE + 5)
    return;
  const int32_t got = value[2] * 3600 + value[1] * 60 + value[0];
  const int32_t want = sent[4] * 3600 + sent[3] * 60 + sent[2];
  int32_t diff = got - want;
  if (diff < -43200)
    diff += 86400;
  if (diff > 43200)
    diff -= 86400;
  if (diff < -CLOCK_CONFIRM_TOLERANCE_S || diff > CLOCK_CONFIRM_TOLERANCE_S)
    return;
  confirm_success_(CONFIRM_ECHO, "Uhrzeit vom Master übernommen");
}

// Uhrzeit-Frame beim Uhrstellen direkt vor dem Senden (auch bei Wiederholung) auf die aktuelle Zeit setzen
void Climos::refresh_pending_clock_() {
  if (!clock_sync_active_ || !seq_active_ || pending_size_ != HEADER_SIZE + 5 ||
      pending_frame_[HEADER_SIZE] != (OBJ_TIME & 0xFF) || pending_frame_[HEADER_SIZE + 1] != (OBJ_TIME >> 8))
    return;
  const uint32_t t = (clock_base_seconds_ + (millis() - clock_base_ms_ + 500) / 1000) % 86400u;
  pending_frame_[HEADER_SIZE + 2] = t % 60;
  pending_frame_[HEADER_SIZE + 3] = (t / 60) % 60;
  pending_frame_[HEADER_SIZE + 4] = t / 3600;
  uint16_t crc = crc16_xmodem_(pending_frame_, 4);
  crc = crc16_xmodem_(pending_frame_ + HEADER_SIZE, pending_size_ - HEADER_SIZE, crc);
  pending_frame_[4] = crc & 0xFF;
  pending_frame_[5] = crc >> 8;
}

bool Climos::request_full_dump() {
  QueuedRequest req;
  req.key = 0x8300;
  req.cmd = 0x83;
  req.label = "Komplett-Abzug anfordern";
  req.frames.emplace_back();
  return enqueue_(std::move(req));
}

bool Climos::send_system_power(bool on) {
  if (on) {
    // Wie das Bedienteil: 0x07 = 1 und 0x0E (Fehlercode) = 0
    const uint8_t payload[] = {static_cast<uint8_t>(OBJ_SYSTEM_POWER & 0xFF), static_cast<uint8_t>(OBJ_SYSTEM_POWER >> 8),
                               0x01, static_cast<uint8_t>(OBJ_ERROR_CODE & 0xFF),
                               static_cast<uint8_t>(OBJ_ERROR_CODE >> 8), 0x00};
    return queue_request_(payload, sizeof(payload), CONFIRM_SYSTEM, 1, "Anlage ein");
  }
  const uint8_t payload[] = {static_cast<uint8_t>(OBJ_SYSTEM_POWER & 0xFF), static_cast<uint8_t>(OBJ_SYSTEM_POWER >> 8),
                             0x00};
  return queue_request_(payload, sizeof(payload), CONFIRM_SYSTEM, 0, "Anlage aus");
}

bool Climos::send_schedule(uint8_t day_mask, const std::string &text) {
  static const char *const DAYS[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  day_mask &= 0x7F;
  // Leerer Text kommt vom Broker beim Abonnieren (ioBroker "publishOnSubscribe" mit leerem Befehls-State):
  // still ignorieren, sonst steht nach jedem Neustart "ungültig" in "Letztes Senden"
  if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
    ESP_LOGD(TAG, "Wochenprogramm: leerer Text ignoriert");
    return false;
  }
  if (day_mask == 0) {
    ESP_LOGW(TAG, "Wochenprogramm: keine Tage gewählt");
    return false;
  }
  std::string group = "Wochenprogramm ";
  if (day_mask == SCHEDULE_MON_FRI) {
    group += "Mo-Fr";
  } else if (day_mask == SCHEDULE_SAT_SUN) {
    group += "Sa-So";
  } else {
    bool first = true;
    for (uint8_t d = 0; d < 7; d++) {
      if (day_mask & (1u << d)) {
        group += (first ? "" : ",");
        group += DAYS[d];
        first = false;
      }
    }
  }
  uint8_t levels[SCHEDULE_SLOTS];
  std::string error;
  if (!parse_schedule_text(text, levels, error)) {
    ESP_LOGW(TAG, "%s nicht gesendet: %s", group.c_str(), error.c_str());
    publish_send_status_(("ungültig: " + group + " (" + error + ")").c_str());
    return false;
  }
  QueuedRequest req;
  req.key = 0x8000 | day_mask;
  req.kind = CONFIRM_ACK;
  req.label = group;
  req.frames = build_schedule_payloads(day_mask, levels);
  return enqueue_(std::move(req));
}

static const size_t MAX_QUEUED_REQUESTS = 8;

bool Climos::queue_request_(const uint8_t *payload, uint8_t size, ConfirmKind kind, uint8_t mode,
                            const std::string &label) {
  QueuedRequest req;
  req.key = size >= 2 ? static_cast<uint16_t>(payload[0] | (payload[1] << 8)) : 0;
  req.kind = kind;
  req.mode = mode;
  req.label = label;
  req.frames.emplace_back(payload, payload + size);
  return enqueue_(std::move(req));
}

bool Climos::enqueue_(QueuedRequest &&req) {
  if (!write_enabled_) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: Senden gesperrt");
    publish_send_status_(("gesperrt: " + req.label).c_str());
    return false;
  }
  if (!sender_.is_ready()) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: kein Sender (tx_pin fehlt oder RMT-Fehler)");
    publish_send_status_(("Fehler (kein Sender): " + req.label).c_str());
    return false;
  }
  for (auto &queued : queue_) {
    if (queued.key == req.key) {
      ESP_LOGI(TAG, "Warteschlange: %s ersetzt %s", req.label.c_str(), queued.label.c_str());
      const std::string label = req.label;
      queued = std::move(req);
      publish_send_status_(("in Warteschlange: " + label).c_str());
      return true;
    }
  }
  if (queue_.size() >= MAX_QUEUED_REQUESTS) {
    ESP_LOGW(TAG, "Warteschlange voll, %s verworfen", req.label.c_str());
    publish_send_status_(("Warteschlange voll: " + req.label).c_str());
    return false;
  }
  const uint16_t key = req.key;
  const std::string label = req.label;
  queue_.push_back(std::move(req));
  start_next_request_();
  for (const auto &queued : queue_) {
    if (queued.key == key) {
      ESP_LOGI(TAG, "Warteschlange: %s wartet (%u Einträge)", label.c_str(), static_cast<unsigned int>(queue_.size()));
      publish_send_status_(("in Warteschlange: " + label).c_str());
      break;
    }
  }
  return true;
}

void Climos::start_next_request_() {
  if (engine_busy_() || queue_.empty() || !write_enabled_)
    return;
  QueuedRequest req = std::move(queue_.front());
  queue_.pop_front();
  if (req.cmd != CMD_DATA) {
    // Komplett-Abzug anfordern: 01 01 83 00 an den Master, ohne Bestätigung
    static const uint8_t NO_PAYLOAD[1] = {0};
    if (!queue_frame_(FRAME_START, 0x01, req.cmd, NO_PAYLOAD, 0)) {
      publish_send_status_(("Fehler: " + req.label).c_str());
      return;
    }
    publish_send_status_(("vorgemerkt (ohne Bestätigung): " + req.label).c_str());
    return;
  }
  if (req.frames.empty())
    return;
  if (req.frames.size() > 1 || req.clock) {
    // Folge (Wochenprogramm, Uhr): läuft vollständig durch, die Warteschlange wartet so lange
    seq_payloads_ = std::move(req.frames);
    seq_index_ = 0;
    seq_label_ = req.label;
    seq_active_ = true;
    clock_sync_active_ = req.clock;
    clock_base_seconds_ = req.clock_base_seconds;
    clock_base_ms_ = req.clock_base_ms;
    char label[64];
    std::snprintf(label, sizeof(label), "%s (1/%u)", seq_label_.c_str(), static_cast<unsigned int>(seq_payloads_.size()));
    if (!start_request_(seq_payloads_[0].data(), seq_payloads_[0].size(), req.kind, req.mode, label, true)) {
      seq_active_ = false;
      clock_sync_active_ = false;
    }
    return;
  }
  start_request_(req.frames[0].data(), req.frames[0].size(), req.kind, req.mode, req.label, false);
}

bool Climos::start_request_(const uint8_t *payload, uint8_t size, ConfirmKind kind, uint8_t mode,
                            const std::string &label, bool keep_sequence) {
  if (!keep_sequence) {
    seq_active_ = false;
    clock_sync_active_ = false;
  }
  if (!write_enabled_) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: Senden gesperrt");
    publish_send_status_(("gesperrt: " + label).c_str());
    return false;
  }
  // Neue Anforderung ersetzt eine noch laufende (wartende oder unbestätigte)
  confirm_kind_ = CONFIRM_NONE;
  confirm_attempt_ = 0;
  acked_by_panel_only_ = false;
  if (!queue_frame_(FRAME_START, ADDR_REGISTERS, CMD_DATA, payload, size)) {
    publish_send_status_(("Fehler: " + label).c_str());
    return false;
  }
  pending_kind_ = kind;
  pending_mode_ = mode;
  pending_label_ = label;
  publish_send_status_(("vorgemerkt: " + label).c_str());
  return true;
}

void Climos::confirm_success_(ConfirmKind kind, const char *how) {
  // Bestätigung kann für das gesendete Frame kommen oder für eines, dessen Wiederholung schon wartet
  const std::string label = confirm_kind_ == kind ? confirm_label_ : pending_label_;
  ESP_LOGI(TAG, "Bestätigt durch %s nach %u ms", how, static_cast<unsigned int>(millis() - last_send_time_));
  if (retry_pending_(kind))
    clear_pending_();
  if (pending_size_ == 0) {
    pending_kind_ = CONFIRM_NONE;
    pending_mode_ = 0;
  }
  confirm_kind_ = CONFIRM_NONE;
  confirm_mode_ = 0;
  confirm_attempt_ = 0;
  acked_by_panel_only_ = false;
  if (kind == CONFIRM_ACK && sent_size_ > HEADER_SIZE) {
    // Einstellwerte meldet kein Gerät zurück, auch nicht bei Änderung am Bedienteil: Dort bestätigt
    // ebenfalls nur das Lüftergerät den Empfang (86/87). Gleiche Bestätigung -> Werte übernehmen.
    parse_records_(sent_frame_ + HEADER_SIZE, sent_size_ - HEADER_SIZE);
    if (settings_dirty_) {
      settings_pref_.save(&settings_);
      settings_dirty_ = false;
    }
  }
  if ((kind == CONFIRM_ACK || kind == CONFIRM_ECHO) && seq_active_) {
    // Mehrteilige Anforderung: nächstes Frame vormerken, erst am Ende "angenommen" für das Ganze
    seq_index_++;
    if (seq_index_ < seq_payloads_.size()) {
      char next[48];
      std::snprintf(next, sizeof(next), "%s (%u/%u)", seq_label_.c_str(), static_cast<unsigned int>(seq_index_ + 1),
                    static_cast<unsigned int>(seq_payloads_.size()));
      ESP_LOGI(TAG, "%s: Frame %u/%u angenommen", seq_label_.c_str(), static_cast<unsigned int>(seq_index_),
               static_cast<unsigned int>(seq_payloads_.size()));
      start_request_(seq_payloads_[seq_index_].data(), seq_payloads_[seq_index_].size(), kind, 0, next, true);
      return;
    }
    seq_active_ = false;
    publish_send_status_(((kind == CONFIRM_ECHO ? "übernommen (Master): " : "angenommen (Lüftergerät): ") + seq_label_).c_str());
    return;
  }
  publish_send_status_(((kind == CONFIRM_ACK ? "angenommen (Lüftergerät): "
                         : kind == CONFIRM_ECHO ? "übernommen (Master): " : "bestätigt: ") + label).c_str());
}

void Climos::publish_send_status_(const char *what) {
  std::string text = what;
  if (!queue_.empty()) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), " (+%u in Warteschlange)", static_cast<unsigned int>(queue_.size()));
    text += buf;
  }
  ESP_LOGI(TAG, "Letztes Senden: %s", text.c_str());
  if (send_status_text_sensor_ != nullptr)
    send_status_text_sensor_->publish_state(text);
}

bool Climos::queue_frame_(uint8_t first, uint8_t dst, uint8_t cmd, const uint8_t *payload, uint8_t payload_size) {
  if (!sender_.is_ready()) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: kein Sender (tx_pin fehlt oder RMT-Fehler)");
    return false;
  }
  if (!write_enabled_) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: Senden gesperrt");
    return false;
  }
  if (payload_size > 0x7F) {
    ESP_LOGW(TAG, "Nicht vorgemerkt: Nutzdaten zu lang (%u)", payload_size);
    return false;
  }
  if (pending_size_ > 0)
    ESP_LOGI(TAG, "Wartendes Frame wird ersetzt: %s", to_hex_(pending_frame_, pending_size_).c_str());

  pending_frame_[0] = first;
  pending_frame_[1] = dst;
  pending_frame_[2] = cmd;
  pending_frame_[3] = payload_size;
  std::memcpy(pending_frame_ + HEADER_SIZE, payload, payload_size);
  uint16_t crc = crc16_xmodem_(pending_frame_, 4);
  crc = crc16_xmodem_(pending_frame_ + HEADER_SIZE, payload_size, crc);
  pending_frame_[4] = crc & 0xFF;
  pending_frame_[5] = crc >> 8;
  pending_size_ = HEADER_SIZE + payload_size;
  pending_since_ = millis();
  pending_kind_ = CONFIRM_NONE;
  pending_mode_ = 0;
  // Ein altes Fenster gilt nicht: erst den nächsten Poll an FF abwarten
  ff_poll_seen_ = false;
  high_freq_.start();
  ESP_LOGI(TAG, "Vorgemerkt, warte auf Sendefenster: %s", to_hex_(pending_frame_, pending_size_).c_str());
  return true;
}

void Climos::clear_pending_() {
  pending_size_ = 0;
  high_freq_.stop();
}

void Climos::loop_confirm_(uint32_t now) {
  if (confirm_kind_ == CONFIRM_NONE || static_cast<int32_t>(now - confirm_deadline_) < 0)
    return;
  const ConfirmKind kind = confirm_kind_;
  const uint8_t mode = confirm_mode_;
  const std::string label = confirm_label_;
  confirm_kind_ = CONFIRM_NONE;
  // Anlage an/aus nie wiederholen: "An" löst eine Neuanmeldung aller Geräte aus
  const uint8_t max_attempts =
      kind == CONFIRM_SYSTEM ? 1 : (kind == CONFIRM_ACK ? SEND_MAX_ATTEMPTS_ACK : SEND_MAX_ATTEMPTS);
  if (confirm_attempt_ >= max_attempts) {
    ESP_LOGW(TAG, "Keine Bestätigung nach %u Versuchen", static_cast<unsigned int>(confirm_attempt_));
    confirm_attempt_ = 0;
    seq_active_ = false;
    publish_send_status_(((acked_by_panel_only_ ? "nur vom Bedienteil quittiert: " : "keine Bestätigung: ") + label)
                             .c_str());
    acked_by_panel_only_ = false;
    return;
  }
  if (!write_enabled_) {
    confirm_attempt_ = 0;
    seq_active_ = false;
    publish_send_status_(("gesperrt: " + label).c_str());
    return;
  }
  // Wiederholen: dasselbe Frame im nächsten Fenster
  ESP_LOGW(TAG, "Keine Bestätigung nach Versuch %u/%u%s, wiederhole", static_cast<unsigned int>(confirm_attempt_),
           static_cast<unsigned int>(max_attempts), acked_by_panel_only_ ? " (nur vom Bedienteil quittiert)" : "");
  std::memcpy(pending_frame_, sent_frame_, sent_size_);
  pending_size_ = sent_size_;
  pending_since_ = now;
  pending_kind_ = kind;
  pending_mode_ = mode;
  pending_label_ = label;
  ff_poll_seen_ = false;
  high_freq_.start();
}

void Climos::loop_send_() {
  const uint32_t now = millis();
  loop_confirm_(now);
  start_next_request_();
  if (pending_size_ == 0)
    return;

  if (!write_enabled_) {
    ESP_LOGW(TAG, "Vorgemerktes Frame verworfen: Senden wurde gesperrt");
    if (pending_kind_ != CONFIRM_NONE)
      publish_send_status_(("gesperrt: " + pending_label_).c_str());
    pending_kind_ = CONFIRM_NONE;
    confirm_attempt_ = 0;
    seq_active_ = false;
    clear_pending_();
    return;
  }
  if (now - pending_since_ > SEND_PENDING_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Vorgemerktes Frame verworfen: in %u s kein Sendefenster",
             static_cast<unsigned int>(SEND_PENDING_TIMEOUT_MS / 1000));
    if (pending_kind_ != CONFIRM_NONE)
      publish_send_status_(("kein Sendefenster: " + pending_label_).c_str());
    pending_kind_ = CONFIRM_NONE;
    confirm_attempt_ = 0;
    seq_active_ = false;
    clear_pending_();
    return;
  }
  // Höchstens ein Frame pro Sekunde
  if (has_sent_ && now - last_send_time_ < SEND_MIN_INTERVAL_MS)
    return;

  // Testsegment: kein Busverkehr, also auch kein Fenster. Sofort senden.
  if (now - last_byte_time_ >= SEND_BUS_SILENCE_MS) {
    transmit_pending_("Testsegment, Bus still", now);
    return;
  }

  if (!ff_poll_seen_)
    return;
  // Seit dem Poll an FF darf kein Byte gekommen sein, auch keins, das noch im UART liegt
  if (rx_bytes_ != ff_poll_rx_bytes_ || !rx_buffer_.empty() || available()) {
    ff_poll_seen_ = false;
    return;
  }
  const uint32_t since_poll = now - ff_poll_time_;
  if (since_poll < SEND_WINDOW_MIN_MS)
    return;
  if (since_poll > SEND_WINDOW_MAX_MS) {
    // Zu spät gesehen (Hauptschleife war blockiert): nächstes Fenster abwarten
    ESP_LOGD(TAG, "Sendefenster verpasst (%u ms nach Poll)", static_cast<unsigned int>(since_poll));
    ff_poll_seen_ = false;
    return;
  }
  char how[40];
  std::snprintf(how, sizeof(how), "Fenster nach FF, +%u ms", static_cast<unsigned int>(since_poll));
  transmit_pending_(how, now);
  ff_poll_seen_ = false;
}

// Auswertung der Quittungsabfragen nach einem eigenen Frame: wen hat der Master gefragt, und hängt das
// davon ab, welches Gerät vor dem Fenster zuletzt gepollt wurde?
void Climos::finish_ack_track_() {
  ack_track_active_ = false;
  const bool fan = ack_track_mask_ & 2;
  if (ack_track_poll_before_ == ADDR_PANEL || ack_track_poll_before_ == ADDR_FAN_UNIT) {
    ack_stats_[ack_track_poll_before_ == ADDR_FAN_UNIT ? 1 : 0][fan ? 1 : 0]++;
  } else {
    ack_stats_other_++;
  }
  ESP_LOGI(TAG, "Quittungsabfrage: 86 an%s%s%s (Poll vor dem Fenster %02X, danach %02X)",
           (ack_track_mask_ & 1) ? " 04" : "", fan ? " 08" : "", ack_track_mask_ == 0 ? " keinen" : "",
           ack_track_poll_before_, ack_track_first_poll_);
  if (ack_stats_text_sensor_ == nullptr)
    return;
  const uint32_t n04 = ack_stats_[0][0] + ack_stats_[0][1];
  const uint32_t n08 = ack_stats_[1][0] + ack_stats_[1][1];
  const uint32_t total = n04 + n08 + ack_stats_other_;
  const uint32_t asked = ack_stats_[0][1] + ack_stats_[1][1];
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%u Frames, 08 gefragt: %u; vor Fenster 04: %u/%u, 08: %u/%u",
                static_cast<unsigned int>(total), static_cast<unsigned int>(asked),
                static_cast<unsigned int>(ack_stats_[0][1]), static_cast<unsigned int>(n04),
                static_cast<unsigned int>(ack_stats_[1][1]), static_cast<unsigned int>(n08));
  ack_stats_text_sensor_->publish_state(buf);
}

bool Climos::transmit_pending_(const char *how, uint32_t now) {
  if (sender_.is_busy())
    return false;
  refresh_pending_clock_();
  if (!sender_.send(pending_frame_, pending_size_, SEND_MARKED_BYTES)) {
    ESP_LOGW(TAG, "Nicht gesendet: RMT hat abgelehnt");
    if (pending_kind_ != CONFIRM_NONE)
      publish_send_status_(("Fehler: " + pending_label_).c_str());
    pending_kind_ = CONFIRM_NONE;
    confirm_attempt_ = 0;
    seq_active_ = false;
    clear_pending_();
    return false;
  }
  if (ack_track_active_)
    finish_ack_track_();
  ack_track_active_ = true;
  ack_track_mask_ = 0;
  ack_track_first_poll_ = 0;
  ack_track_poll_before_ = poll_before_ff_;
  has_sent_ = true;
  last_send_time_ = now;
  watching_ = true;
  watch_until_ = now + SEND_WATCH_MS;
  last_watch_log_.clear();
  std::memcpy(sent_frame_, pending_frame_, pending_size_);
  sent_size_ = pending_size_;
  // Log erst nach dem Start, das Frame läuft in Hardware weiter
  ESP_LOGI(TAG, "Gesendet (%s, %u RMT-Symbole, %u µs): %s", how,
           static_cast<unsigned int>(sender_.last_symbol_count()),
           static_cast<unsigned int>(BusEncoder::frame_duration_us(pending_size_)),
           to_hex_(pending_frame_, pending_size_).c_str());
  if (pending_kind_ != CONFIRM_NONE) {
    confirm_kind_ = pending_kind_;
    confirm_mode_ = pending_mode_;
    confirm_label_ = pending_label_;
    confirm_attempt_++;
    confirm_deadline_ = now + (confirm_kind_ == CONFIRM_SYSTEM ? SYSTEM_CONFIRM_MS
                               : confirm_kind_ == CONFIRM_ECHO ? ECHO_CONFIRM_MS : SEND_CONFIRM_MS);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "gesendet: %s (Versuch %u)", confirm_label_.c_str(),
                  static_cast<unsigned int>(confirm_attempt_));
    publish_send_status_(buf);
  }
  pending_kind_ = CONFIRM_NONE;
  pending_mode_ = 0;
  clear_pending_();
  return true;
}

uint16_t Climos::crc16_xmodem_(const uint8_t *data, size_t size, uint16_t crc) {
  for (size_t i = 0; i < size; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  return crc;
}

}  // namespace climos
}  // namespace esphome
