#pragma once
#include <atomic>
#ifdef TSAN_MODEL
extern "C" void __tsan_acquire(void*); extern "C" void __tsan_release(void*);
extern int g_tsan_sync;
static inline void __dmb() { __tsan_release(&g_tsan_sync); __tsan_acquire(&g_tsan_sync); std::atomic_thread_fence(std::memory_order_seq_cst); }
#else
static inline void __dmb() { std::atomic_thread_fence(std::memory_order_seq_cst); }
#endif
