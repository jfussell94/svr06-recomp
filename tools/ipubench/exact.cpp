// exhaustive check: every (Y, Cb, Cr) through both paths
#define main ipu_unused_main
#include "SvrIpu.cpp"
#undef main
#include <cstdio>
int main(){
  using namespace svr06ipu_impl;
  uint64_t bad=0;
  for(int cb=0;cb<256;cb++) for(int cr=0;cr<256;cr++){
    for(int i=0;i<8;i++) for(int j=0;j<8;j++){ decoder.mb8.Cb[i][j]=cb; decoder.mb8.Cr[i][j]=cr; }
    for(int y=0;y<256;y++) decoder.mb8.Y[y>>4][y&15]=y;
    yuv2rgb_reference(); macroblock_rgb32 a=decoder.rgb32;
    std::memset(&decoder.rgb32,0x55,sizeof decoder.rgb32);
    yuv2rgb_sse2();
    if(std::memcmp(&a,&decoder.rgb32,sizeof a)) { if(bad<5) printf("mismatch cb=%d cr=%d\n",cb,cr); ++bad; }
  }
  // varied chroma inside one macroblock (layout / duplication check)
  srand(7); for(int t=0;t<200000;t++){ uint8_t*m=(uint8_t*)&decoder.mb8; for(int i=0;i<384;i++) m[i]=rand();
    yuv2rgb_reference(); macroblock_rgb32 a=decoder.rgb32; yuv2rgb_sse2(); if(std::memcmp(&a,&decoder.rgb32,sizeof a)) { if(bad<10) printf("random mismatch t=%d\n",t); ++bad; } }
  printf("exhaustive 2^24 + 200000 random macroblocks: %llu mismatches\n",(unsigned long long)bad);
}
int dither_check(){
  using namespace svr06ipu_impl; uint64_t bad=0; srand(9);
  for(int t=0;t<300000;t++){ macroblock_rgb32 in; uint8_t*m=(uint8_t*)&in; for(size_t i=0;i<sizeof in;i++) m[i]=rand();
    if(t&1) for(int k=0;k<256;k++) ((uint8_t*)&in.c[0][0])[k*4+3]= (rand()&1)?0x40:0x80; // realistic alpha
    if(t%5==0) for(size_t i=0;i<sizeof in;i++) if(i%4!=3) m[i]= (rand()&1)? (uint8_t)(rand()%5) : (uint8_t)(251+rand()%5); // clamp edges
    for(int dte=0;dte<2;dte++){ macroblock_rgb16 a,b; memset(&a,0,sizeof a); memset(&b,0x77,sizeof b); ipu_dither_reference(in,a,dte); ipu_dither_sse2(in,b,dte); if(memcmp(&a,&b,sizeof a)){ if(bad<5) printf("dither mismatch t=%d dte=%d\n",t,dte); ++bad;} } }
  printf("dither 300000x2 macroblocks: %llu mismatches\n",(unsigned long long)bad); return 0; }
static int s_dc = dither_check();
