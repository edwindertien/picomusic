#include "wavetable.h"
#include <math.h>
int16_t Wavetable::_sine[WAVETABLE_SIZE];
void Wavetable::init() {
    for (int i = 0; i < WAVETABLE_SIZE; i++)
        _sine[i] = (int16_t)(sinf((float)i / (float)WAVETABLE_SIZE * 2.0f * (float)M_PI) * 32767.0f);
}
