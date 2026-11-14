#pragma once
#include <Arduino.h>
#include "config.h"
void midi_note_on(uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t note);
void midi_pitch_bend(int16_t bend);
void midi_cc(uint8_t cc, uint8_t value);
void midi_program_change(uint8_t program);

inline void midi_handler_init() {}
inline void midi_handler_poll() {}
