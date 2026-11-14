#pragma once
// ============================================================
//  bass_storage.h  —  save / load the sound + patterns to flash (LittleFS)
//
//  The image itself (the image, CRC32) is produced and validated by BassManager::serialize / deserialize,
//  which are host-tested including corruption. This file only moves bytes to and from a file.
//
//  RULES
//   * Core 0 only. A flash erase/program stalls BOTH cores for milliseconds (the audio core runs from RAM but
//     the other core must be parked): the UI and the serial command therefore REFUSE to save while playing.
//   * Written to a temp file and renamed over the old one (LittleFS replaces the destination atomically), so a power
//     cut at ANY point leaves either the old or the new save intact. Never remove the old file first.
//   * Needs a filesystem partition: platformio.ini sets board_build.filesystem_size.
//   * Load happens once at boot; a missing or invalid file just leaves the factory demo in place.
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "bass_manager.h"
#if ENABLE_STORAGE
#  include <LittleFS.h>
#endif

static bool g_fsOk = false;

inline void storage_init() {
#if ENABLE_STORAGE
    g_fsOk = LittleFS.begin();                  // formats the partition on first use
#endif
}

inline bool storage_save(BassManager& m) {
#if ENABLE_STORAGE
    if (!g_fsOk || m.playing) return false;
    static uint8_t buf[BassManager::SAVE_SIZE];
    const size_t n = m.serialize(buf, sizeof buf);
    if (n != BassManager::SAVE_SIZE) return false;
    File f = LittleFS.open(STORAGE_FILE ".tmp", "w");
    if (!f) return false;
    const size_t w = f.write(buf, n);
    f.close();
    if (w != n) { LittleFS.remove(STORAGE_FILE ".tmp"); return false; }
    return LittleFS.rename(STORAGE_FILE ".tmp", STORAGE_FILE);
#else
    (void)m; return false;
#endif
}

inline bool storage_load(BassManager& m) {
#if ENABLE_STORAGE
    if (!g_fsOk) return false;
    File f = LittleFS.open(STORAGE_FILE, "r");
    if (!f) return false;
    static uint8_t buf[BassManager::SAVE_SIZE + 1];
    const size_t n = f.read(buf, sizeof buf);
    f.close();
    return m.deserialize(buf, n);               // false (and nothing changed) if the image is not valid
#else
    (void)m; return false;
#endif
}
