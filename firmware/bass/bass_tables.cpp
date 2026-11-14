#include <math.h>
#include "bass_tables.h"
float BT_G[BT_NF + 1];
float BT_K[BT_NF + 1];
float BT_E2[258];
void bass_tables_init() {
    for (int i = 0; i <= BT_NF; i++) {
        const double f = FILTER_MIN_HZ * pow(2.0, (double)i / FILTER_STEPS_PER_OCTAVE);
        const double w = 2.0 * M_PI * f / SAMPLE_RATE;
        const double g = 1.0 - cos(w) + sin(w);                      // see bass_tables.h
        const double s2 = sin(w) * sin(w);
        BT_G[i] = (float)g;
        BT_K[i] = (float)(4.0 * s2 * s2 / (g * g * g * g));
    }
    for (int i = 0; i < 258; i++) BT_E2[i] = (float)pow(2.0, i / 256.0);
}
