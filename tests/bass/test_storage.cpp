// bass_storage.h (the real wrapper) against an in-memory LittleFS: save, load, refusal while playing, power cut, corruption.
#include <Arduino.h>
#define BASS_TRACE 1
#define private public
#include "bass_manager.h"
#undef private
#include "bass_storage.h"
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main() {
    bass_tables_init(); static BassManager a, b; a.init(); b.init();
    CHECK(!storage_load(b), "load before init / with no file fails cleanly");
    storage_init(); CHECK(LittleFS.began, "filesystem mounted");
    CHECK(!storage_load(b), "no file yet: load fails, factory demo stays");
    a.setBpm(133); a.setPattern(9); a.setStep(7, 50, 3); a.loadPatch(3); a.setParam(P_CUT, 12);
    a.playing = 1; CHECK(!storage_save(a) && LittleFS.files.empty(), "save REFUSED while playing (flash write would stall the audio)"); a.playing = 0;
    CHECK(storage_save(a), "save"); CHECK(LittleFS.files.count(STORAGE_FILE) == 1 && LittleFS.files.count(STORAGE_FILE ".tmp") == 0 && LittleFS.files[STORAGE_FILE].size() == BassManager::SAVE_SIZE, "one file of BassManager::SAVE_SIZE bytes, no temp left over");
    CHECK(storage_load(b) && b.bpm == 133.0f && b.stepNote[9][7] == 50 && b.stepFlg[9][7] == 3 && b.pv[P_CUT] == 12 && b.patch == 3 && b.selPat == 9, "load restores everything");
    // power cut between writing the temp file and the rename: the OLD save must still be there and valid
    a.setBpm(77); LittleFS.cutAfterTmp = true; CHECK(!storage_save(a), "rename fails (power cut)");
    LittleFS.cutAfterTmp = false; static BassManager c; c.init(); CHECK(storage_load(c) && c.bpm == 133.0f, "the previous save survived the failed one (bpm %.0f)", c.bpm);
    LittleFS.failWrite = true; CHECK(!storage_save(a), "a failed write is reported"); LittleFS.failWrite = false;
    CHECK(LittleFS.files.count(STORAGE_FILE ".tmp") == 0, "failed write leaves no temp file");
    // corruption in flash
    LittleFS.files[STORAGE_FILE][LittleFS.files[STORAGE_FILE].size() / 2] ^= 0x40; static BassManager d; d.init(); const float before = d.bpm; CHECK(!storage_load(d) && d.bpm == before, "bit-rot: load fails and changes nothing");
    LittleFS.files[STORAGE_FILE].resize(100); CHECK(!storage_load(d), "truncated file rejected");
    LittleFS.files[STORAGE_FILE].assign(5000, 0xFF); CHECK(!storage_load(d), "erased flash rejected");
    printf(fails ? "\n%d STORAGE TESTS FAILED\n" : "\nALL STORAGE TESTS PASS\n", fails); return fails != 0;
}
