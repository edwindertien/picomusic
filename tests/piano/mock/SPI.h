// Host SPI mock: forwards every transfer to a hook (the LCD emulator) and counts calls.
#pragma once
#include "Arduino.h"
struct SPISettings { SPISettings(uint32_t = 0, int = 0, int = 0) {} };
struct SPIClass {
    uint64_t calls = 0, bytes = 0;
    void (*hook)(const uint8_t* p, size_t n) = nullptr;
    void setTX(int) {} void setSCK(int) {} void setRX(int) {} void begin() {}
    void beginTransaction(SPISettings) {} void endTransaction() {}
    uint8_t transfer(uint8_t d) { calls++; bytes++; if (hook) hook(&d, 1); return 0; }
    void transfer(const void* tx, void* /*rx*/, size_t n) { calls++; bytes += n; if (hook) hook((const uint8_t*)tx, n); }
};
extern SPIClass SPI1;
