#pragma once
#include <Arduino.h>

//! ── Schneider EasyLogic PM2200 driver ────────────────────────────────────────
//
// Reads the meter over RS485 Modbus RTU (rs485_sensor.cpp transport) and keeps the
// latest values here for the MQTT payload (local_mqtt.cpp) and the setup web UI.
// Field names match the payload keys in docs/payload.md. A value stays NAN until
// it has been read (or when its register address is still unknown, see pm2200_map.h).
//
// In simulation mode (NVS "sim", default PM2200_SIM_DEFAULT) the same fields are
// filled by a generator instead of the meter, so WiFi/MQTT can be tested with no meter.

//! ── Latest meter values (units as in the payload) ───────────────────────────
struct Pm2200Reading {
    float v1, v2, v3, v_avg;                      // V   L-N
    float v12, v23, v31, vll_avg;                 // V   L-L
    float i1, i2, i3, i_avg, i_n;                 // A
    float p1, p2, p3, p_total_w;                  // W
    float q1, q2, q3, q_total_var;                // var
    float s1, s2, s3, s_total_va;                 // VA
    float pf1, pf2, pf3, pf;                      // true PF
    float freq_hz;                                // Hz
    float i_unbal_pct, v_unbal_ll_pct, v_unbal_ln_pct;   // %
    float energy_wh, energy_wh_recv;              // Wh   delivered / received
    float energy_vah, energy_vah_recv;            // VAh
    float energy_varh, energy_varh_recv;          // varh
};

extern Pm2200Reading pm2200;
extern bool pm2200SensorOK;      // RS485 layer healthy (false after RS485_MAX_FAILS consecutive failures)
extern bool pm2200MeterOK;       // last poll returned valid values

//! ── API ──────────────────────────────────────────────────────────────────────
void pm2200Init();               // call once in setup() — loads settings from NVS, opens the UART
void pm2200Read();               // call in loop() — internally throttled (5s)

// `,"key":value` for every field that has a value (the MQTT payload and the setup page share this);
// returns the characters written, or -1 if they do not fit in `cap`.
int pm2200ReadingJson(char* out, size_t cap);

// Meter settings (NVS namespace "pm2200"); the installer matches these to the meter's front panel.
uint8_t  pm2200Addr();           // Modbus slave address
uint32_t pm2200Baud();           // 4800 / 9600 / 19200 / 38400
char     pm2200Parity();         // 'E' / 'O' (1 stop bit) / 'N' (2 stop bits)
bool     pm2200Simulated();      // true = generated values, no meter attached
void     pm2200ApplyConfig(uint8_t addr, uint32_t baud, char parity, bool sim);   // validate, save, reopen UART
const char* pm2200StatusLabel(); // "simulated" / "ok" / "no response" — for UI/logs
