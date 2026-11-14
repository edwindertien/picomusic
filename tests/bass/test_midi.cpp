// The REAL midi_handler.h parsers (UART byte stream and USB packets) with MIDI_REALTIME on.
#include <Arduino.h>
#include <vector>
#include <string>
#include "config.h"
#include "midi_handler.h"
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static std::string log_;
void midi_note_on(uint8_t n, uint8_t v)  { log_ += "N" + std::to_string(n) + "/" + std::to_string(v) + " "; }
void midi_note_off(uint8_t n)            { log_ += "O" + std::to_string(n) + " "; }
void midi_pitch_bend(int16_t b)          { log_ += "B" + std::to_string(b) + " "; }
void midi_cc(uint8_t c, uint8_t v)       { log_ += "C" + std::to_string(c) + "/" + std::to_string(v) + " "; }
void midi_program_change(uint8_t p)      { log_ += "P" + std::to_string(p) + " "; }
void midi_realtime(uint8_t s)            { char b[8]; snprintf(b, sizeof b, "R%02X ", s); log_ += b; }
volatile uint8_t g_cpu_load_pct = 0;
static std::string uart(const std::vector<uint8_t>& bytes) { log_.clear(); _uartStatus = 0; _uartIdx = 0; _uartExpect = 0; for (uint8_t b : bytes) _parseUartByte(b); return log_; }
static std::string usb(std::vector<std::array<uint8_t, 4>> pk) { log_.clear(); for (auto& p : pk) _dispatchUsbPacket(p.data()); return log_; }

int main() {
    // ---- UART ----
    CHECK(uart({0x99, 36, 100}) == "N36/100 ", "plain note");
    CHECK(uart({0xF8, 0xF8, 0xFA, 0xFC, 0xFB}) == "RF8 RF8 RFA RFC RFB ", "realtime bytes are forwarded: %s", log_.c_str());
    CHECK(uart({0x99, 36, 0xF8, 100}) == "RF8 N36/100 ", "clock BETWEEN the data bytes of a note: note intact (%s)", log_.c_str());
    CHECK(uart({0x99, 0xF8, 36, 0xF8, 100, 0xF8}) == "RF8 RF8 N36/100 RF8 ", "clock after the status byte and the data bytes (%s)", log_.c_str());
    CHECK(uart({0x99, 36, 100, 0xF8, 38, 90}) == "N36/100 RF8 N38/90 ", "RUNNING STATUS survives a realtime byte (%s)", log_.c_str());
    CHECK(uart({0x99, 36, 100, 38, 90, 40, 80}) == "N36/100 N38/90 N40/80 ", "running status itself (%s)", log_.c_str());
#ifndef TEST_CH10        // (these use channel 1: omni build only)
    CHECK(uart({0xB0, 1, 0xFA, 64}) == "RFA C1/64 ", "start inside a CC (%s)", log_.c_str());
#endif
    CHECK(uart({0xF9, 0xFD, 0xFE, 0xFF}) == "", "undefined / active-sensing / reset bytes ignored, nothing forwarded (%s)", log_.c_str());
    CHECK(uart({0x99, 36, 100, 0xFE, 0x99, 37, 90}) == "N36/100 N37/90 ", "active sensing between messages (%s)", log_.c_str());
#ifndef TEST_CH10
    CHECK(uart({0xC0, 5}) == "P5 ", "program change");
#endif
    CHECK(uart({0xF0, 1, 2, 3, 0xF7, 0x99, 36, 100}) == "N36/100 ", "sysex skipped (%s)", log_.c_str());
    std::vector<uint8_t> flood; for (int i = 0; i < 2000; i++) { flood.push_back(0x99); flood.push_back(36 + i % 16); flood.push_back(1); if (i % 3 == 0) flood.push_back(0xF8); }
    uart(flood); int notes = 0, clocks = 0; for (size_t p = 0; (p = log_.find('N', p)) != std::string::npos; p++) notes++; for (size_t p = 0; (p = log_.find("RF8", p)) != std::string::npos; p++) clocks++;
    CHECK(notes == 2000 && clocks == 667, "2000 notes + 667 clocks interleaved: %d notes, %d clocks", notes, clocks);
    // ---- USB packets (cin 0xF = one-byte system message) ----
    CHECK(usb({{0x09, 0x99, 36, 100}}) == "N36/100 ", "USB note");
    CHECK(usb({{0x0F, 0xF8, 0, 0}, {0x0F, 0xFA, 0, 0}, {0x0F, 0xFC, 0, 0}, {0x0F, 0xFB, 0, 0}}) == "RF8 RFA RFC RFB ", "USB realtime: %s", log_.c_str());
    CHECK(usb({{0x0F, 0xFE, 0, 0}, {0x0F, 0xFF, 0, 0}, {0x0F, 0xF9, 0, 0}}) == "", "USB active sensing / reset ignored");
#ifndef TEST_CH10
    CHECK(usb({{0x0B, 0xB0, 1, 50}, {0x0F, 0xF8, 0, 0}, {0x0B, 0xB0, 2, 60}}) == "C1/50 RF8 C2/60 ", "USB: clock between CCs: %s", log_.c_str());
#endif
#ifdef TEST_CH10
    // ---- build with MIDI_CHANNEL = 10: channel messages are filtered, real-time messages must NOT be (0xF8 & 0x0F = 8, "channel 9") ----
    CHECK(uart({0x99, 36, 100}) == "N36/100 ", "channel 10 note passes");
    CHECK(uart({0x90, 36, 100}) == "", "channel 1 note filtered (%s)", log_.c_str());
    CHECK(uart({0x90, 36, 0xF8, 100}) == "RF8 ", "clock inside a FILTERED note still arrives, the note stays filtered (%s)", log_.c_str());
    CHECK(uart({0xF8, 0xFA, 0xFC}) == "RF8 RFA RFC ", "UART real-time passes the channel filter (%s)", log_.c_str());
    CHECK(usb({{0x0F, 0xF8, 0, 0}, {0x0F, 0xFA, 0, 0}, {0x0F, 0xFC, 0, 0}}) == "RF8 RFA RFC ", "USB real-time passes the channel filter (%s)", log_.c_str());
    CHECK(usb({{0x09, 0x90, 36, 100}}) == "" && usb({{0x09, 0x99, 36, 100}}) == "N36/100 ", "USB channel filter still works");
#else
    printf("(run again with -DTEST_CH10 and MIDI_CHANNEL 10 for the channel-filter cases)\n");
#endif
    printf(fails ? "\n%d MIDI TESTS FAILED\n" : "\nALL MIDI TESTS PASS\n", fails); return fails != 0;
}
