// Host shim for rendering the tonewheel engine: std::min/max and single-thread no-op spin locks (the real ones are pico-sdk).
#include <algorithm>
using std::min; using std::max;
typedef int spin_lock_t;
static int _spin_dummy;
static inline int spin_lock_claim_unused(bool) { return 0; }
static inline spin_lock_t* spin_lock_init(int) { return &_spin_dummy; }
static inline void spin_lock_unsafe_blocking(spin_lock_t*) {}
static inline void spin_unlock_unsafe(spin_lock_t*) {}
static inline bool spin_try_lock_unsafe(spin_lock_t*) { return true; }
