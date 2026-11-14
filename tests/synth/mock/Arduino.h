// Minimal host mock of the Arduino-pico API surface used by the synth sources.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <atomic>

typedef uint8_t byte;
#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define SPI_MODE0 0
#define __not_in_flash_func(f) f
#define F_CPU 240000000L
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))

// ---- controllable clock --------------------------------------------------
#include <chrono>
#include <thread>
extern uint64_t g_mock_us;
extern bool     g_mock_real;                         // threaded test: use the real clock
extern void   (*g_delay_us_hook)(uint32_t);          // single-thread test: run audio while core 0 "waits"
inline uint64_t mock_real_us() { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline uint32_t micros()  { return g_mock_real ? (uint32_t)mock_real_us() : (uint32_t)g_mock_us; }
inline uint32_t millis()  { return g_mock_real ? (uint32_t)(mock_real_us() / 1000) : (uint32_t)(g_mock_us / 1000); }
inline void delay(uint32_t ms)            { if (g_mock_real) std::this_thread::sleep_for(std::chrono::milliseconds(ms)); else g_mock_us += (uint64_t)ms * 1000; }
inline void delayMicroseconds(uint32_t u) {
    if (g_mock_real) { std::this_thread::sleep_for(std::chrono::microseconds(u)); return; }
    g_mock_us += u; if (g_delay_us_hook) g_delay_us_hook(u); }

// ---- GPIO (recorded so the LCD emulator can watch DC / CS) ---------------
extern uint8_t g_pin[64];
extern void (*g_pin_hook)(int pin, int val);
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { g_pin[p] = (uint8_t)v; if (g_pin_hook) g_pin_hook(p, v); }
inline int  digitalRead(int p) { return g_pin[p]; }

// ---- String (subset) -------------------------------------------------------
class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c) {}
    String(const std::string& x) : s(x) {}
    unsigned length() const { return (unsigned)s.size(); }
    void trim() { size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { s.clear(); return; }
                  size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); }
    bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
    String substring(unsigned a) const { return String(s.substr(a)); }
    String substring(unsigned a, unsigned b) const { return String(s.substr(a, b - a)); }
    int indexOf(char c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    long toInt() const { return atol(s.c_str()); }
    const char* c_str() const { return s.c_str(); }
    String& operator+=(char c) { s += c; return *this; }
    String& operator=(const char* c) { s = c; return *this; }
    bool operator==(const char* c) const { return s == c; }
    bool operator==(const String& o) const { return s == o.s; }
};

// ---- Serial (captured) ------------------------------------------------------
struct MockSerial {
    std::string out; std::string in;
    void begin(int) {}
    void print(const char* c) { out += c; }
    void print(int v) { out += std::to_string(v); }
    void print(unsigned v) { out += std::to_string(v); }
    void print(long v) { out += std::to_string(v); }
    void print(unsigned long v) { out += std::to_string(v); }
    void println(unsigned v) { out += std::to_string(v) + "\n"; }
    void println(long v) { out += std::to_string(v) + "\n"; }
    void println(unsigned long v) { out += std::to_string(v) + "\n"; }
    void println(const char* c) { out += c; out += "\n"; }
    void println(const String& s) { out += s.s; out += "\n"; }
    void println(int v) { out += std::to_string(v) + "\n"; }
    void println(float v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); out += b; out += "\n"; }
    void print(float v, int d = 2)   { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); out += b; }
    int printf(const char* f, ...) __attribute__((format(printf, 2, 3))) {
        char b[512]; va_list a; va_start(a, f); int n = vsnprintf(b, sizeof b, f, a); va_end(a); out += b; return n; }
    int available() { return (int)in.size(); }
    int read() { if (in.empty()) return -1; int c = (unsigned char)in[0]; in.erase(0, 1); return c; }
};
extern MockSerial Serial;
