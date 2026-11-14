#pragma once
// Sine table only (vibrato LFO and ensemble LFOs). Storage lives in wavetable.cpp (defined once).
#include <stdint.h>
#include "config.h"
class Wavetable {
public:
    static void init();                                   // call once from core 0, before audio starts
    static inline int16_t sine(uint32_t idx) { return _sine[idx & (WAVETABLE_SIZE - 1)]; }
    static int16_t _sine[WAVETABLE_SIZE];
};
