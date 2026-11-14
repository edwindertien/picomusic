// Host mock of the pico-sdk PIO API: records exactly what the driver programs into the state machine.
#pragma once
#include <cstdint>
#include <vector>
typedef unsigned int uint;
typedef volatile uint32_t io_rw_32;
struct pio_program { const uint16_t* instructions; uint8_t length; int8_t origin; uint32_t pio_version; };
struct pio_sm_config { float clkdiv = 1; uint wrap_target = 0, wrap = 31; uint side_bits = 0; bool side_opt = false, side_dir = false;
                       uint out_base = 0, out_cnt = 0, set_base = 0, set_cnt = 0, side_base = 0; bool shift_right = true, autopull = false; uint thresh = 32; };
struct pio_hw_t { uint32_t ctrl; struct { io_rw_32 clkdiv; io_rw_32 addr; } sm[4]; };
typedef pio_hw_t* PIO;
extern pio_hw_t g_mock_pio[3];
#define pio0 (&g_mock_pio[0])
#define pio1 (&g_mock_pio[1])
#define pio2 (&g_mock_pio[2])
struct PioCapture { std::vector<uint16_t> program; uint offset = 0; pio_sm_config cfg; uint initial_pc = 0; int exec_jmp = -1;
                    uint32_t dir_mask = 0, dir_val = 0, pins_val = 99; std::vector<uint> gpio_init; bool enabled = false; std::vector<uint32_t> words; };
extern PioCapture g_pio_cap;
inline uint pio_claim_unused_sm(PIO, bool) { return 0; }
inline uint pio_add_program(PIO, const pio_program* p) { g_pio_cap.program.assign(p->instructions, p->instructions + p->length); g_pio_cap.offset = 0; return 0; }
inline pio_sm_config pio_get_default_sm_config() { return pio_sm_config(); }
inline void sm_config_set_wrap(pio_sm_config* c, uint t, uint w) { c->wrap_target = t; c->wrap = w; }
inline void sm_config_set_sideset(pio_sm_config* c, uint bits, bool opt, bool dir) { c->side_bits = bits; c->side_opt = opt; c->side_dir = dir; }
inline void sm_config_set_out_pins(pio_sm_config* c, uint base, uint n) { c->out_base = base; c->out_cnt = n; }
inline void sm_config_set_set_pins(pio_sm_config* c, uint base, uint n) { c->set_base = base; c->set_cnt = n; }
inline void sm_config_set_sideset_pins(pio_sm_config* c, uint base) { c->side_base = base; }
inline void sm_config_set_out_shift(pio_sm_config* c, bool right, bool autopull, uint thr) { c->shift_right = right; c->autopull = autopull; c->thresh = thr; }
inline void sm_config_set_clkdiv(pio_sm_config* c, float d) { c->clkdiv = d; }
inline void pio_gpio_init(PIO, uint pin) { g_pio_cap.gpio_init.push_back(pin); }
inline void pio_sm_init(PIO p, uint sm, uint pc, const pio_sm_config* c) {
    g_pio_cap.cfg = *c; g_pio_cap.initial_pc = pc;
    uint32_t ip = (uint32_t)c->clkdiv, fr = (uint32_t)((c->clkdiv - ip) * 256.0f); p->sm[sm].clkdiv = (ip << 16) | (fr << 8); }
inline void pio_sm_set_pindirs_with_mask(PIO, uint, uint32_t v, uint32_t m) { g_pio_cap.dir_val = v; g_pio_cap.dir_mask = m; }
inline void pio_sm_set_pins(PIO, uint, uint32_t v) { g_pio_cap.pins_val = v; }
inline uint32_t pio_encode_jmp(uint addr) { return addr & 0x1f; }
inline void pio_sm_exec(PIO, uint, uint32_t instr) { if ((instr & 0xE000) == 0) g_pio_cap.exec_jmp = (int)(instr & 0x1f); }
inline void pio_sm_set_enabled(PIO p, uint sm, bool en) { g_pio_cap.enabled = en; if (en) p->ctrl |= 1u << sm; else p->ctrl &= ~(1u << sm); }
inline void pio_sm_put_blocking(PIO, uint, uint32_t w) { g_pio_cap.words.push_back(w); }
inline uint pio_sm_get_tx_fifo_level(PIO, uint) { return 0; }
