#pragma once
#include <Arduino.h>
#include "config.h"
extern std::atomic<int> g_put_count;
extern int16_t g_last_put;
inline void audio_driver_init(uint32_t) {}
inline void audio_driver_put(int16_t l, int16_t) { g_last_put = l; g_put_count++; }
inline void audio_driver_debug() { Serial.println("--- PIO debug (mock) ---"); }
