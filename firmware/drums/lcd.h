#pragma once
// ============================================================
//  lcd.h  —  public interface of the LCD panel + joystick/buttons
//  Implementation: lcd.cpp
//
//  The panel polls the machine state itself and redraws only what changed, so callers do not have to
//  mark anything dirty when a CC or a pad hit arrives.
// ============================================================
#include <stdint.h>

class DrumManager;                        // forward declaration

void lcd_begin();                         // init SPI1, panel and buttons
void ui_flush(const DrumManager& machine);// call every loop(): cheap when idle, time-sliced when not
void ui_handle_buttons(DrumManager& machine);
void ui_midi_activity();                  // flash the MIDI dot
void ui_mark_all_dirty();                 // force a full redraw

// implemented in main.cpp (drum_storage.h): write / read the pattern + kit file. Return true on success.
bool storage_save(DrumManager& machine);
bool storage_load(DrumManager& machine);
