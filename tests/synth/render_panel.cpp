#include <Arduino.h>
#include <SPI.h>
#include "synth_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
SynthManager synth;
#include "lcd.cpp"
static uint16_t scr[LCD_W*LCD_H]; static int dcv=1,csv=1,cmdc=-1,pn=0; static uint8_t pb[4]; static int cx0,cx1,ry0,ry1,wx,wy,half=0; static uint8_t hib=0;
static void pinHook(int p,int v){ if(p==LCD_DC)dcv=v; if(p==LCD_CS)csv=v; }
static void spiHook(const uint8_t*d,size_t n){ if(csv)return; for(size_t i=0;i<n;i++){ uint8_t b=d[i];
  if(!dcv){cmdc=b;pn=0;if(b==0x2C){wx=cx0;wy=ry0;half=0;}continue;}
  if(cmdc==0x2A||cmdc==0x2B){pb[pn++]=b;if(pn==4){int a=(pb[0]<<8)|pb[1],c=(pb[2]<<8)|pb[3];if(cmdc==0x2A){cx0=a-40;cx1=c-40;}else{ry0=a-53;ry1=c-53;}pn=0;}}
  else if(cmdc==0x2C){ if(!half){hib=b;half=1;} else {half=0; if(wx>=0&&wx<LCD_W&&wy>=0&&wy<LCD_H&&wx<=cx1&&wy<=ry1) scr[wy*LCD_W+wx]=(hib<<8)|b; if(++wx>cx1){wx=cx0;wy++;}}}}}
static void settle(){ for(int k=0;k<200;k++){ g_mock_us+=25000; ui_flush(synth);} }
static void save(const char*p){ FILE*f=fopen(p,"wb"); for(int i=0;i<LCD_W*LCD_H;i++){uint16_t c=scr[i];fputc((c>>11)<<3,f);fputc(((c>>5)&63)<<2,f);fputc((c&31)<<3,f);} fclose(f); }
int main(){
  Wavetable::init(); for(int p:{2,18,16,20,3,15,17}) g_pin[p]=1; g_pin_hook=pinHook; SPI1.hook=spiHook;
  synth.init(); lcd_begin();
  // representative state: Init patch lightly edited, a 5-note chord, a little pitch bend, cutoff selected
  synth.loadPatch(1);
  synth.pitchBend(2300); _sel=P_CUTOFF; g_cpu_load_pct=37;
  for(int n:{48,55,60,64,67}) synth.noteOn(n,100);
  for(int i=0;i<3000;i++) synth.tick();
  ui_midi_activity(); g_mock_us+=1000; settle();   // dot is lit at the time of the last scan
  ui_midi_activity(); synth.setGlide(false); ui_flush(synth); g_mock_us+=25000; ui_flush(synth); for(int i=0;i<20;i++){ g_mock_us+=1000; ui_flush(synth);} 
  save("panel_clean.rgb");
  // second render: PATCH name 'Init' state with MIDI dot on at scan time
  printf("patch=%s sel=%d counted=%d\n", synth.patchName, _sel, synth.countedVoices());

  // ---- second state: "while playing" — unedited Init patch, 8-note chord, bend down, CPU 58 % (orange), RES selected ----
  synth.panic(); for(int i=0;i<6000;i++) synth.tick();
  synth.loadPatch(0); synth.setGlide(false); synth.pitchBend(-3300); _sel=P_RES; g_cpu_load_pct=58;
  for(int n:{40,47,52,55,59,64,67,71}) synth.noteOn(n,100);
  for(int i=0;i<3000;i++) synth.tick();
  ui_midi_activity(); g_mock_us+=1000; settle();
  ui_midi_activity(); ui_flush(synth); g_mock_us+=25000; ui_flush(synth); for(int i=0;i<20;i++){ g_mock_us+=1000; ui_flush(synth);} 
  save("panel_played.rgb");
  printf("patch=%s sel=%d counted=%d\n", synth.patchName, _sel, synth.countedVoices());
}
