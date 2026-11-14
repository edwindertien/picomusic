#pragma once
// Minimal stand-ins so the REAL midi_handler.h compiles on the PC. Packets are fed through g_usb_pkts.
#include <Arduino.h>
#include <deque>
#include <array>
struct Adafruit_USBD_MIDI {
    void begin() {}
    bool readPacket(uint8_t* p) { if (g_usb_pkts().empty()) return false; auto a = g_usb_pkts().front(); g_usb_pkts().pop_front(); for (int i = 0; i < 4; i++) p[i] = a[i]; return true; }
    static std::deque<std::array<uint8_t, 4>>& g_usb_pkts() { static std::deque<std::array<uint8_t, 4>> q; return q; }
};
struct TinyUSBDeviceMock { bool mounted() { return true; } void setManufacturerDescriptor(const char*) {} void setProductDescriptor(const char*) {} };
inline TinyUSBDeviceMock TinyUSBDevice;
struct MockSerial2 { std::deque<uint8_t> in; void setRX(int) {} void begin(int) {} int available() { return (int)in.size(); } int read() { int c = in.front(); in.pop_front(); return c; } };
inline MockSerial2 Serial2;
