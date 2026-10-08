#pragma once

// RC522 wiring.  The reader must be powered from 3.3 V, never 5 V.
constexpr uint8_t RFID_SS_PIN = 5;
constexpr uint8_t RFID_SCK_PIN = 18;
constexpr uint8_t RFID_MOSI_PIN = 23;
constexpr uint8_t RFID_MISO_PIN = 19;
constexpr uint8_t RFID_RST_PIN = 22;

constexpr char AP_SSID[] = "Asistencia-RFID";
constexpr char AP_PASSWORD[] = "cambiar-esta-clave"; // Change before a public demo.
constexpr uint32_t SESSION_DURATION_MS = 10UL * 60UL * 1000UL;
constexpr size_t MAX_STUDENTS = 80;
constexpr size_t MAX_PRESENT = 80;

