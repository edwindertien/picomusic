// drum_hex.h (the serial `dump` / `image <hex>` helpers) and the full image path: serialize -> hex -> decode -> deserialize.
#include <Arduino.h>
#define private public
#include "drum_manager.h"
#undef private
#include "drum_hex.h"
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main() {
    Wavetable::init(); static DrumManager a, b; a.init(); b.init();
    a.setBpm(141); a.setPattern(11); a.setStep(4, 9, 3); a.setSongSlot(0, 11); a.selectInst(7); a.setParam(P_DECAY, 99);
    static uint8_t img[DrumManager::SAVE_SIZE], back[DrumManager::SAVE_SIZE + 1]; static char hex[2 * DrumManager::SAVE_SIZE + 2];
    const size_t n = a.serialize(img, sizeof img); CHECK(n == 4247, "image is 4247 bytes");
    const size_t hn = drumHexEncode(img, n, hex); hex[hn] = 0; CHECK(hn == 2 * 4247, "8494 hex characters");
    size_t m = 0; CHECK(drumHexDecode(hex, hn, back, sizeof back, m) && m == n && !memcmp(img, back, n), "decode returns the same bytes");
    CHECK(b.deserialize(back, m) && b.bpm == 141.0f && b.pat[11][4][9] == 3 && b.song[0] == 11 && b.kit[7][2] == 99, "machine B restores machine A from the hex line");
    for (size_t i = 0; i < hn; i++) if (hex[i] >= 'a' && hex[i] <= 'f') hex[i] -= 32;
    CHECK(drumHexDecode(hex, hn, back, sizeof back, m) && m == n && !memcmp(img, back, n), "upper-case hex is accepted");
    CHECK(!drumHexDecode(hex, hn - 1, back, sizeof back, m), "odd length rejected");
    hex[10] = 'g'; CHECK(!drumHexDecode(hex, hn, back, sizeof back, m), "non-hex character rejected");
    hex[10] = '0'; CHECK(!drumHexDecode(hex, hn, back, 100, m), "longer than the buffer rejected (no overflow)");
    CHECK(drumHexDecode("", 0, back, sizeof back, m) && m == 0 && !b.deserialize(back, 0), "empty line: decodes to nothing, deserialize refuses");
    hex[100] = hex[100] == '0' ? '1' : '0'; drumHexDecode(hex, hn, back, sizeof back, m); static DrumManager c; c.init(); const float before = c.bpm;
    CHECK(!c.deserialize(back, m) && c.bpm == before, "a damaged image is refused and changes nothing");
    printf(fails ? "\n%d HEX TESTS FAILED\n" : "\nALL HEX TESTS PASS\n", fails); return fails != 0;
}
