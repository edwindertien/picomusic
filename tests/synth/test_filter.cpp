// Compare the NEW filter against the OLD one (same coefficient law) and check stability.
#include <Arduino.h>
#include "config.h"
#include "synth_filter.h"          // new (staged)
// ---- old filter, verbatim, in its own namespace so both can coexist ----
namespace oldf {
  #include "old/synth_filter_body.h"
}
#include <cstdio>

static float newGain(float nomHz, float testHz, float res){
    SynthFilter f; f.reset(SynthFilter::coeffForHz(nomHz), res*4.0f);
    const int N=44100; double acc=0; int cnt=0;
    for(int n=0;n<N;n++){ float x=0.2f*sinf(2*3.14159265f*testHz*n/44100.0f); float y=f.tick(x); if(n>N/2){acc+=y*y;cnt++;} }
    return (float)(sqrt(acc/cnt)/(0.2/sqrt(2.0)));
}
static float find3dB(float nom){ for(float tf=20; tf<20000; tf*=1.03f) if(newGain(nom,tf,0)<0.707f) return tf; return -1; }

int main(){
    int fails=0;
    printf("== NEW filter: -3dB point vs nominal (old measured: 200->44, 500->111, 1000->226, 2000->473, 4000->1050, 8000->3326) ==\n");
    float noms[]={200,500,1000,2000,4000,8000}; float oldv[]={44,111,226,473,1050,3326};
    for(int i=0;i<6;i++){ float f3=find3dB(noms[i]); float err=fabsf(f3-oldv[i])/oldv[i];
        printf("  nominal %5.0f -> %6.0f Hz   (old %5.0f, diff %.1f%%)%s\n",noms[i],f3,oldv[i],err*100, err>0.06f?"  <-- DIFFERS":""); if(err>0.06f) fails++; }

    printf("\n== tan() approximation error (Pade) vs tanf, 20 Hz .. FILTER_MAX_HZ ==\n");
    float worst=0; for(float hz=20; hz<=FILTER_MAX_HZ; hz*=1.01f){ float a=SynthFilter::coeffForHz(hz), b=tanf(3.14159265f*hz/44100.0f); float e=fabsf(a-b)/b; if(e>worst) worst=e; }
    printf("  worst relative error: %.3f %%  (limit 1%%)\n",worst*100); if(worst>0.01f) fails++;

    printf("\n== STABILITY: saw @110Hz amp 1.2*1.2 through filter, peak|y| (old: 2.8 @16k, 5.1 @18k, 11.6 @20k) ==\n");
    float hzs[]={4000,8000,12000,14000,16000,18000,20000,40000};
    for(float hz: hzs){
        printf("  requested %6.0f Hz (clamped to %.0f): ",hz, hz>FILTER_MAX_HZ?FILTER_MAX_HZ:hz);
        for(float r: {0.0f,0.5f,0.95f}){
            SynthFilter f; f.reset(SynthFilter::coeffForHz(hz), r*4.0f);
            float peak=0; bool bad=false; double ph=0;
            for(int n=0;n<44100;n++){ ph+=110.0/44100.0; if(ph>=1)ph-=1; float y=f.tick((float)(2*ph-1)*1.2f); if(!(fabsf(y)<1e3f)) bad=true; if(fabsf(y)>peak) peak=fabsf(y); }
            printf(" res%.2f peak=%.2f%s",r,peak,bad?" BAD":""); if(bad||peak>2.0f) fails++;
        }
        printf("\n");
    }

    printf("\n== sane()/recovery: poison a state with NaN and with 1e9 ==\n");
    { SynthFilter f; f.reset(0.2f,0); f.s2=NAN; if(f.sane()){printf("  NaN not detected!\n"); fails++;} else printf("  NaN detected\n");
      f.s2=0; f.s4=1e9f; if(f.sane()){printf("  runaway not detected!\n"); fails++;} else printf("  runaway detected\n"); }

    printf("\nFILTER TESTS: %s (%d failures)\n", fails?"FAIL":"PASS", fails); return fails?1:0;
}
