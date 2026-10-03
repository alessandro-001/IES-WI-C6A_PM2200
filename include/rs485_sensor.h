#pragma once
#include <Arduino.h>


//! ── RS485 Modbus RTU Transport ───────────────────────────────────────────────
//
// Hardware (IES-WI-C6A, see schematic):
//   MAX3485 DI  <- GPIO16 (TXD0 pad, driven as UART1 via GPIO matrix)
//   MAX3485 RO  -> GPIO17 (RXD0 pad, driven as UART1 via GPIO matrix)
//   MAX3485 DE+RE (RS485_FC) <- GPIO14   HIGH = transmit, LOW = receive
//
// Only ONE meter is ever connected (single-drop bus): the Schneider PM2200,
// read by pm2200.cpp on top of modbusReadHolding().

//! ── Diagnostics ──────────────────────────────────────────────────────────────
extern uint32_t rs485PollCount;  // total poll attempts since boot
extern uint32_t rs485FailCount;  // total failed polls since boot (timeout/CRC/invalid)

//! ── API ──────────────────────────────────────────────────────────────────────
void rs485SensorInit();                 // call once in setup() — DE/RE pin to receive
void rs485BeginUart(uint32_t baud, uint32_t serialCfg);   // (re)open UART1, serialCfg = SERIAL_8E1 / 8O1 / 8N2 ...
bool modbusReadHolding(uint8_t addr, uint16_t startReg, uint16_t count, uint16_t* out);   // FC 0x03, count <= 32
