#include "SvrIpu.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static uint32_t ch3[4], ch4[6];
static int irqs[8];
static void irq(void*, uint32_t c){ irqs[c&7]++; }
static uint64_t fnv(const uint8_t*p,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ull;}return h;}
int main(int argc,char**argv){
  int ofm = argc>1?atoi(argv[1]):0, dte = argc>2?atoi(argv[2]):0, iters= argc>3?atoi(argv[3]):200;
  std::vector<uint8_t> ram(32u<<20), spr(16384);
  svr06ipu::Host h; h.rdram=ram.data(); h.spr=spr.data();
  for(int i=0;i<4;i++) h.ch3[i]=&ch3[i]; for(int i=0;i<6;i++) h.ch4[i]=&ch4[i]; h.dmacIrq=irq;
  svr06ipu::bind(h);
  const int MBC=1120; // one 640x448 frame
  srand(1); uint8_t* in=ram.data()+0x100000; for(int i=0;i<MBC*384;i++) in[i]= (i%7==0)? 0 : (uint8_t)rand();
  const uint32_t outBytes = MBC*(ofm?512:1024); uint8_t* out=ram.data()+0x800000;
  uint64_t hash=0; auto t0=std::chrono::steady_clock::now();
  for(int it=0;it<iters;it++){
    svr06ipu::write32(0x10002010,0x40000000u); // CTRL RST
    svr06ipu::write32(0x10002000, (7u<<28)|((uint32_t)dte<<26)|((uint32_t)ofm<<27)|MBC);
    ch4[0]=0; ch4[1]=0x100000; ch4[2]=MBC*384/16; ch4[0]=0x101; svr06ipu::dmaStart(4);
    ch3[0]=0; ch3[1]=0x800000; ch3[2]=outBytes/16; ch3[0]=0x100; svr06ipu::dmaStart(3);
    for(int k=0;k<100000 && ((ch3[0]&0x100)||(ch4[0]&0x100));k++) svr06ipu::poll();
  }
  auto t1=std::chrono::steady_clock::now();
  hash=fnv(out,outBytes);
  double us=std::chrono::duration<double,std::micro>(t1-t0).count();
  printf("ofm=%d dte=%d frames=%d  %.1f us/frame  %.3f us/MB  hash=%016llx ch3qwc=%u ch4qwc=%u irq3=%d irq4=%d nz=%d\n",ofm,dte,iters,us/iters,us/iters/MBC,(unsigned long long)hash,ch3[2],ch4[2],irqs[3],irqs[4],(int)(out[0]|out[100]|out[5000]));
}
