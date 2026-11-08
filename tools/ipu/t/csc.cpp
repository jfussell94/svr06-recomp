#include "../SvrIpu.h"
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<uint8_t> ram(32u<<20), spr(16384);
static uint32_t c3[4], c4[6];
static void irq(void*, uint32_t){}
int main(){
  svr06ipu::Host h; h.rdram=ram.data(); h.spr=spr.data();
  for(int i=0;i<4;i++) h.ch3[i]=&c3[i]; for(int i=0;i<6;i++) h.ch4[i]=&c4[i]; h.dmacIrq=irq; svr06ipu::bind(h);
  svr06ipu::write32(0x10002010, 1u<<30);
  uint8_t *mb=&ram[0x100000];
  memset(mb,150,256); memset(mb+256,60,64); memset(mb+320,200,64); // Y=150 Cb=60 Cr=200 -> reddish
  svr06ipu::write32(0x10002000, 0);
  c3[1]=0x200000; c3[2]=64; c3[0]=0x100; svr06ipu::dmaStart(3);
  c4[1]=0x100000; c4[2]=24; c4[0]=0x101; svr06ipu::dmaStart(4);
  svr06ipu::write32(0x10002000, (7u<<28)|1);
  uint8_t *o=&ram[0x200000];
  printf("ctrl=%08X out px0 = %d %d %d %d  ch3 left=%u\n", svr06ipu::read32(0x10002010), o[0],o[1],o[2],o[3], c3[2]);
  int lum=(0x95*(150-16))>>6, cr=200-128, cb=60-128;
  printf("expect r=%d g=%d b=%d\n",(lum+((0xcc*cr)>>6)+1)>>1,(lum+((-0x68*cr)>>6)+((-0x32*cb)>>6)+1)>>1,(lum+((0x102*cb)>>6)+1)>>1);
}
