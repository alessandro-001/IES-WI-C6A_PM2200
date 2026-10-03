#include "rs485_sensor.h"
#include "config.h"

#include <Arduino.h>
#include <HardwareSerial.h>

//* RS485 Modbus RTU transport — used by the PM2200 driver (pm2200.cpp)
//* Single shared Modbus master, FC 0x03 only. Register maps and scaling live in the driver.

// ── Diagnostics ──────────────────────────────────────────────────────────────
uint32_t rs485PollCount = 0;
uint32_t rs485FailCount = 0;

// ── Module-private state ─────────────────────────────────────────────────────
static HardwareSerial RS485(1);          // UART1 routed to TXD0/RXD0 pads via GPIO matrix

static bool     _uartReady     = false;

// ── CRC-16/Modbus (poly 0xA001, init 0xFFFF, transmitted low byte first) ────
static uint16_t modbusCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

// ── UART / transceiver helpers ───────────────────────────────────────────────
void rs485BeginUart(uint32_t baud, uint32_t serialCfg) {
    RS485.end();
    delay(10);
    RS485.begin(baud, serialCfg, RS485_RX_PIN, RS485_TX_PIN);
    RS485.setTimeout(RS485_TIMEOUT_MS);
    _uartReady = true;
    Serial.printf("[RS485] UART1 up — %lu baud, cfg 0x%lX (TX=GPIO%d RX=GPIO%d DE/RE=GPIO%d)\n",
                  (unsigned long)baud, (unsigned long)serialCfg, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);
}

static void rs485Flush() {
    while (RS485.available()) RS485.read();
}

// ── Core Modbus transaction: FC 0x03 Read Holding Registers ─────────────────
// Returns true and fills `out[count]` (big-endian words decoded) on success.
bool modbusReadHolding(uint8_t addr, uint16_t startReg, uint16_t count, uint16_t* out) {
    if (!_uartReady || count == 0 || count > 32) return false;

    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x03;
    req[2] = (uint8_t)(startReg >> 8);
    req[3] = (uint8_t)(startReg & 0xFF);
    req[4] = (uint8_t)(count >> 8);
    req[5] = (uint8_t)(count & 0xFF);
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);   // CRC low byte first
    req[7] = (uint8_t)(crc >> 8);

    rs485Flush();

    // Transmit (DE/RE high)
    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(100);                 // transceiver enable settle
    RS485.write(req, sizeof(req));
    RS485.flush();                          // wait until shifted out of UART
    delayMicroseconds(100);                 // last stop bit margin
    digitalWrite(RS485_DE_PIN, LOW);        // back to receive

    // Receive: addr + fc + bytecount + 2*count data + 2 CRC
    const size_t expected = 5 + (size_t)count * 2;
    uint8_t buf[5 + 2 * 32];
    size_t  got   = 0;
    uint32_t t0   = millis();

    while (got < expected && (millis() - t0) < RS485_TIMEOUT_MS) {
        int avail = RS485.available();
        while (avail-- > 0 && got < sizeof(buf)) {
            buf[got++] = (uint8_t)RS485.read();
        }
        if (got < expected) delay(2);
    }

    if (got == 0) {
        Serial.println("[RS485] timeout — no response (meter missing / wiring / baud?)");
        return false;
    }

    // Modbus exception frame? addr, fc|0x80, code, crc(2)
    if (got >= 5 && buf[0] == addr && buf[1] == (0x80 | 0x03)) {
        Serial.printf("[RS485] Modbus exception 0x%02X from addr %u\n", buf[2], addr);
        return false;
    }

    if (got < expected) {
        Serial.printf("[RS485] short frame: got %u of %u bytes\n",
                      (unsigned)got, (unsigned)expected);
        return false;
    }

    if (buf[0] != addr || buf[1] != 0x03 || buf[2] != (uint8_t)(count * 2)) {
        Serial.printf("[RS485] bad header: %02X %02X %02X (want %02X 03 %02X)\n",
                      buf[0], buf[1], buf[2], addr, count * 2);
        return false;
    }

    uint16_t rxCrc   = (uint16_t)buf[expected - 2] | ((uint16_t)buf[expected - 1] << 8);
    uint16_t calcCrc = modbusCrc16(buf, expected - 2);
    if (rxCrc != calcCrc) {
        Serial.printf("[RS485] CRC fail: rx=0x%04X calc=0x%04X — frame discarded\n",
                      rxCrc, calcCrc);
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        out[i] = ((uint16_t)buf[3 + i * 2] << 8) | buf[4 + i * 2];
    }
    return true;
}

// ── Init ─────────────────────────────────────────────────────────────────────
void rs485SensorInit() {
    Serial.println();
    Serial.println("### RS485 MODBUS DRIVER INIT ###");

    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);   // receive mode ASAP (FC may float high at boot)
}
