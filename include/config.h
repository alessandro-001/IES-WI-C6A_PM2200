#pragma once

#include "secrets.h"

//! ── Access Point ─────────────────────────────────────────────────────────────
#define AP_SSID             "PM2200_Hotspot"

//! ── Hardware Pins ESP32-C6 ───────────────────────────────────────────────────
#define NEOPIXEL_PIN        20 // 3 (old)
#define NUM_LEDS            12
#define BRIGHTNESS          10

#define FACTORY_RESET_PIN   5 // 4 (old)

//! ── Sensor & LED Timing ──────────────────────────────────────────────────────
#define SENSOR_INTERVAL     5000
#define LED_INTERVAL        20

//! (Provisioning host and platform-specific defines removed)

//! ── Device Identity ──────────────────────────────────────────────────────────
#define DEVICE_GROUP        "PROTO_BF_DEVICES"
#define FIRMWARE_VERSION    "1.0.0"

//! ── Device Type ──────────────────────────────────────────────────────────────
// Fixed: this firmware is the PM2200 power meter only (no runtime sensor switching).
#define SENSOR_TYPE_ID      4
#define SENSOR_TYPE_LABEL   "power"
#define DEVICE_ID_PREFIX    "PM_"      // device_id = prefix + last 4 MAC hex chars
#define MQTT_MEASUREMENT    "power"    // sensors/<device_id>/power

//! ── Local MQTT (Raspberry Pi / Docker) ──────────────────────────────────────
#define LOCAL_MQTT_SERVER   "weedsync.local"
#define LOCAL_MQTT_PORT     1883

//! ── mDNS ─────────────────────────────────────────────────────────────────────
#define MDNS_PREFIX         "bossfarm"

//! ── RS485 / Modbus RTU (MAX3485, see schematic) ─────────────────────────────
#define RS485_TX_PIN          16    // TXD0 pad -> MAX3485 DI  (driven as UART1)
#define RS485_RX_PIN          17    // RXD0 pad <- MAX3485 RO  (driven as UART1)
#define RS485_DE_PIN          14    // RS485_FC -> DE+RE, HIGH=TX LOW=RX (module pin 19)

#define RS485_TIMEOUT_MS      400   // per-transaction response timeout
#define RS485_POLL_INTERVAL   SENSOR_INTERVAL   // poll every 5s
#define RS485_MAX_FAILS       3     // consecutive failures before flagged unavailable

//! ── PM2200 defaults (overridden by NVS "pm2200", set from the setup page) ───
// The manual does not state the meter's defaults: match these to its front panel (Comm setup).
#define PM2200_ADDR_DEFAULT   1
#define PM2200_BAUD_DEFAULT   9600  // 4800 / 9600 / 19200 / 38400
#define PM2200_PARITY_DEFAULT 'E'   // 'E' / 'O' = 1 stop bit, 'N' = 2 stop bits
#define PM2200_SIM_DEFAULT    true  // true = generated values, no meter attached (set false for the real meter)