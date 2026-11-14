#pragma once
#include <cstdint>
enum { clk_sys = 5 };
inline uint32_t clock_get_hz(int) { return 240000000u; }
