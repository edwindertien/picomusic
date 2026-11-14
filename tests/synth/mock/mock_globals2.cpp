#include <atomic>
#include <cstdint>
std::atomic<int> g_put_count{0};
int16_t g_last_put = 0;
