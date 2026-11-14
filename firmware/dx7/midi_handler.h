#pragma once
// ============================================================
//  midi_handler.h  –  MIDI input: USB-MIDI + UART MIDI
//
//  Supports two simultaneous MIDI sources:
//    1. USB-MIDI  (class-compliant, no driver needed on host)
//    2. UART MIDI (DIN-5 via optocoupler on UART1, GP4/GP5)
//       GP4 = TX (not used for receive, but set for completeness)
//       GP5 = RX
//
//  Calls midi_note_on(note, velocity) / midi_note_off(note)
//  which you implement in main.cpp.
//
//  Single-note implementation: only the most recent note plays.
//  Polyphony is added in the next step.
// ============================================================

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "config.h"
#include "dx7_patch.h"

#ifndef MIDI_REALTIME
#define MIDI_REALTIME 0     // 1: forward clock / start / continue / stop to midi_realtime()
#endif

// ---- Forward declarations (implemented in main.cpp) --------
void midi_note_on(uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t note);
void midi_pitch_bend(int16_t bend);
void midi_cc(uint8_t cc, uint8_t value);
void midi_program_change(uint8_t program);
#if MIDI_REALTIME
void midi_realtime(uint8_t status);
#endif
void midi_sysex_bank(const uint8_t* data4096);        // a complete, checksum-verified DX7 32-voice bank (implemented in main.cpp)

// ---- SysEx: DX7 32-voice bank dump  F0 43 0n 09 20 00 <4096 data bytes> <checksum> F7 -----------------------------------
//  Bytes arrive one at a time from the UART or from USB-MIDI packets. Any status byte other than real-time aborts the message.
//  The 4 KB buffer is static; a bank arrives in about 1.4 s over a DIN cable, instantly over USB.
#define SYSEX_HDR 5          // 43 0n 09 20 00
#define SYSEX_DATA 4096
namespace {
    uint8_t  _sx[SYSEX_HDR + SYSEX_DATA + 1];
    uint16_t _sxN = 0;
    bool     _sxOn = false;
    void _sysexByte(uint8_t b) {
        if (b == 0xF0) { _sxOn = true; _sxN = 0; return; }
        if (!_sxOn) return;
        if (b == 0xF7) {
            _sxOn = false;
            if (_sxN == sizeof _sx && _sx[0] == 0x43 && (_sx[1] & 0xF0) == 0x00 && _sx[2] == 0x09 && _sx[3] == 0x20 && _sx[4] == 0x00 &&
                dx7BankChecksumOk(_sx + SYSEX_HDR, _sx[SYSEX_HDR + SYSEX_DATA]))
                midi_sysex_bank(_sx + SYSEX_HDR);
            return;
        }
        if (b & 0x80) { _sxOn = false; return; }                     // another status byte: the message was cut off
        if (_sxN < sizeof _sx) _sx[_sxN++] = b; else _sxOn = false;      // too long: not ours
    }
}

// ---- USB-MIDI ----------------------------------------------
Adafruit_USBD_MIDI usb_midi_iface;

// ---- UART MIDI (31250 baud on UART1) -----------------------
// Uses Serial2 in earlephilhower (maps to UART1)
// Wire a 6N138 optocoupler: DIN-5 pin2→GND, pin4→optocoupler,
// output → GP5.  TX (GP4) unused for standard MIDI-in.
#define MIDI_UART_BAUD  31250
#define MIDI_UART_RX    5     // GP5

// ---- Simple UART MIDI parser state machine -----------------
namespace {
    uint8_t _uartStatus  = 0;
    uint8_t _uartData[2] = {0, 0};
    uint8_t _uartIdx     = 0;
    uint8_t _uartExpect  = 0;   // expected data bytes for current status

    uint8_t _expectedBytes(uint8_t status) {
        uint8_t type = status & 0xF0;
        switch (type) {
            case 0x80: return 2;  // note off
            case 0x90: return 2;  // note on
            case 0xA0: return 2;  // aftertouch
            case 0xB0: return 2;  // CC
            case 0xC0: return 1;  // program change
            case 0xD0: return 1;  // channel pressure
            case 0xE0: return 2;  // pitch bend
            default:   return 0;
        }
    }

    void _dispatchUart(uint8_t status, uint8_t d0, uint8_t d1) {
        uint8_t ch   = (status & 0x0F) + 1;
        if (MIDI_CHANNEL != 0 && ch != MIDI_CHANNEL) return;
        uint8_t type = status & 0xF0;
        switch (type) {
            case 0x90:
                if (d1 > 0) { midi_note_on(d0, d1); break; }
                [[fallthrough]];                      // note-on with velocity 0 is a note-off
            case 0x80:
                midi_note_off(d0);
                break;
            case 0xB0:
                midi_cc(d0, d1);
                break;
            case 0xC0:
                midi_program_change(d0);
                break;
            case 0xE0: {
                int16_t bend = (int16_t)((d1 << 7) | d0) - 8192;
                midi_pitch_bend(bend);
                break;
            }
            default: break;
        }
    }

    void _parseUartByte(uint8_t b) {
        if (b >= 0xF8) {
            // System real-time byte. The MIDI spec lets these appear ANYWHERE, even between the data bytes of
            // another message, and they must not disturb that message or the running status: handle and return.
#if MIDI_REALTIME
            if (b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC) midi_realtime(b);
#endif
            return;
        }
        if (b == 0xF0 || (_sxOn && b == 0xF7)) { _sysexByte(b); _uartStatus = 0; _uartExpect = 0; return; }
        if (_sxOn && !(b & 0x80)) { _sysexByte(b); return; }
        if (b & 0x80) {
            if (_sxOn) _sxOn = false;                                // a status byte cuts a SysEx short
            // Status byte
            _uartStatus = b;
            _uartIdx    = 0;
            _uartExpect = _expectedBytes(b);
        } else {
            // Data byte
            if (_uartExpect == 0) return;
            _uartData[_uartIdx++] = b;
            if (_uartIdx >= _uartExpect) {
                _dispatchUart(_uartStatus,
                              _uartData[0],
                              _uartExpect > 1 ? _uartData[1] : 0);
                _uartIdx = 0;   // running status: ready for next message
            }
        }
    }
}

// ---- USB-MIDI packet parser --------------------------------
namespace {
    void _dispatchUsbPacket(uint8_t pkt[4]) {
        uint8_t cin    = pkt[0] & 0x0F;
        uint8_t status = pkt[1];
        uint8_t d0     = pkt[2];
        uint8_t d1     = pkt[3];
#if MIDI_REALTIME
        if (cin == 0xF) {                         // single-byte system real-time: not a channel message, skip the channel filter
            if (status == 0xF8 || status == 0xFA || status == 0xFB || status == 0xFC) midi_realtime(status);
            return;
        }
#endif
        if (cin >= 0x4 && cin <= 0x7) {                           // SysEx: 4 = three bytes follow, 5 = one byte ends it, 6 = two bytes end it, 7 = three bytes end it
            const uint8_t n = (cin == 0x4 || cin == 0x7) ? 3 : (cin == 0x6 ? 2 : 1);
            for (uint8_t i = 0; i < n; i++) _sysexByte(pkt[1 + i]);
            return;
        }
        uint8_t ch     = (status & 0x0F) + 1;
        if (MIDI_CHANNEL != 0 && ch != MIDI_CHANNEL) return;
        switch (cin) {
            case 0x9:
                if (d1 > 0) { midi_note_on(d0, d1); break; }
                [[fallthrough]];                      // note-on with velocity 0 is a note-off
            case 0x8:
                midi_note_off(d0);
                break;
            case 0xB:
                midi_cc(d0, d1);
                break;
            case 0xC:
                midi_program_change(d0);
                break;
            case 0xE: {
                int16_t bend = (int16_t)((d1 << 7) | d0) - 8192;
                midi_pitch_bend(bend);
                break;
            }
            default: break;
        }
    }
}

// ---- Public API --------------------------------------------

inline void midi_handler_init()
{
    // USB-MIDI
    usb_midi_iface.begin();
    TinyUSBDevice.setManufacturerDescriptor("picomusic");
    TinyUSBDevice.setProductDescriptor("Pico DX7");

    // UART MIDI on Serial2 (UART1, GP4/GP5)
    pinMode(MIDI_UART_RX, INPUT_PULLUP);
    Serial2.setRX(MIDI_UART_RX);
    Serial2.begin(MIDI_UART_BAUD);
}

inline void midi_handler_poll()
{
    // -- USB-MIDI --
    if (TinyUSBDevice.mounted()) {
        uint8_t pkt[4];
        while (usb_midi_iface.readPacket(pkt)) {
            _dispatchUsbPacket(pkt);
        }
    }

    // -- UART MIDI --
    while (Serial2.available()) {
        _parseUartByte((uint8_t)Serial2.read());
    }
}