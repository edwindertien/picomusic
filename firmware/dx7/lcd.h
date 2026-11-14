#pragma once
// ============================================================
//  lcd.h  —  public interface of the LCD panel + joystick/buttons. Implementation: lcd.cpp
//  The panel polls the machine state itself and redraws only what changed.
// ============================================================
#include <stdint.h>

class Dx7Manager;                         // forward declaration

void lcd_begin();                         // init SPI1, panel and buttons
void ui_flush(const Dx7Manager& machine); // call every loop(): cheap when idle, time-sliced when not
void ui_handle_buttons(Dx7Manager& machine);
void ui_midi_activity();                  // flash the MIDI dot
void ui_mark_all_dirty();                 // force a full redraw
