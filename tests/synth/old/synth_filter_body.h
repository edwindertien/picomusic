#include <math.h>
#include <stdint.h>
#define SAMPLE_RATE_OLD 44100
class SynthFilter {
public:
    void setResonance(float r) {
        r = r < 0.0f ? 0.0f : r > 0.99f ? 0.99f : r;
        if (r != _res) { _res = r; }
    }

    // Call once per sample from voice tick()
    inline void setCutoff(uint8_t cc, float envAmt, float envLevel) {
        float hz = _ccToHz(cc) + envAmt * envLevel * 12000.0f;
        if (hz < 20.0f)    hz = 20.0f;
        if (hz > 20000.0f) hz = 20000.0f;

        // Quantise to integer Hz — avoids tanf() on tiny float changes
        uint16_t hzQ = (uint16_t)hz;
        if (hzQ == _lastHz) return;  // no change → skip tanf entirely
        _lastHz = hzQ;

        float fc = hz / (float)SAMPLE_RATE_OLD;
        if (fc > 0.49f) fc = 0.49f;
        _g = tanf((float)M_PI * fc);  // only called when Hz changes
    }

    // Snap immediately at noteOn — no slew, no stale _lastHz
    void snapCutoff(uint8_t cc, float envAmt, float envLevel) {
        float hz = _ccToHz(cc) + envAmt * envLevel * 12000.0f;
        if (hz < 20.0f)    hz = 20.0f;
        if (hz > 20000.0f) hz = 20000.0f;
        _lastHz = (uint16_t)hz;
        float fc = hz / (float)SAMPLE_RATE_OLD;
        if (fc > 0.49f) fc = 0.49f;
        _g = tanf((float)M_PI * fc);
    }

    inline float tick(float in) {
        float g   = _g;
        float res = _res * 4.0f;
        float fb  = res * _s4;
        float u   = in - fb;

        // 4 one-pole Huovilainen stages
        float t0 = _tanh(u),  t1 = _tanh(_s1),
              t2 = _tanh(_s2), t3 = _tanh(_s3), t4 = _tanh(_s4);
        _s1 += g * (t0 - t1);
        _s2 += g * (t1 - t2);
        _s3 += g * (t2 - t3);
        _s4 += g * (t3 - t4);

        return _s4;
    }

    void reset() {
        _s1=_s2=_s3=_s4=0.0f;
        _g=0.2f; _res=0.0f; _lastHz=0;
    }

private:
    float    _s1=0,_s2=0,_s3=0,_s4=0;
    float    _g=0.2f, _res=0.0f;
    uint16_t _lastHz = 0;

    static float _ccToHz(uint8_t cc) {
        // 30Hz at CC=0, ~18kHz at CC=127, log curve via fast 2^x
        return 30.0f * _pow2fast(cc * (1.0f/18.0f));
    }

    static float _pow2fast(float x) {
        int xi=(int)x; float xf=x-xi;
        float p=1+xf*(0.6931472f+xf*(0.2402265f+xf*0.0557602f));
        union{float f;int32_t i;}u; u.i=(xi+127)<<23; return u.f*p;
    }

    static inline float _tanh(float x) {
        if (x >  4.0f) return  1.0f;
        if (x < -4.0f) return -1.0f;
        float x2 = x*x;
        return x*(27.0f+x2)/(27.0f+9.0f*x2);
    }
};
