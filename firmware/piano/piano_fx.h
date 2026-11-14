#pragma once
// ============================================================
//  piano_fx.h  —  the piano's name for the effects pipeline, which lives in firmware/shared/stereo_fx.h (the DX7 uses the same one):
//
//      voices -> volume -> WAH -> TREMOLO / AUTO-PAN -> CHORUS -> REVERB -> soft limiter
//
//  This wrapper only maps the piano's 16-knob table onto the eight effect knobs.
// ============================================================
#include "config.h"
#include "piano_params.h"
#include "stereo_fx.h"

struct PianoFx : StereoFx {
    void setParams(const uint8_t* pv, int wposOverride = -1) {
        setKnobs(pv[P_WAH], (uint8_t)(wposOverride >= 0 ? wposOverride : pv[P_WPOS]), pv[P_WLFO], pv[P_TREM], pv[P_TRATE], pv[P_CHOR], pv[P_REV], pv[P_RSIZE]);
    }
};
