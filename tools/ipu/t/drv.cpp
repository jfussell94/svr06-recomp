#include "../SvrIpu.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <algorithm>
static std::vector<uint8_t> ram(32u<<20), spr(16384);
static uint32_t c3[4], c4[6];
static int irqs[8];
static std::vector<std::vector<uint8_t>> mbs; static void hook(const void*p){ mbs.emplace_back((const uint8_t*)p,(const uint8_t*)p+384);} 
static void irq(void*, uint32_t c){ irqs[c&7]++; }
int main(){
  std::ifstream f("in.m1v",std::ios::binary); std::vector<uint8_t> es((std::istreambuf_iterator<char>(f)),{});
  svr06ipu::Host h; h.rdram=ram.data(); h.spr=spr.data();
  for(int i=0;i<4;i++) h.ch3[i]=&c3[i]; for(int i=0;i<6;i++) h.ch4[i]=&c4[i];
  h.dmacIrq=irq; svr06ipu::bind(h); svr06ipu::setDebugMbHook(hook);
  const int W=96,H=64,MBW=W/16,MBH=H/16;
  std::vector<uint8_t> rgb(W*H*4,0);
  svr06ipu::write32(0x10002010, 1u<<30);           // reset
  svr06ipu::write32(0x10002010, 1u<<23);           // MP1
  {
    static const uint8_t defIntra[64]={8,16,16,19,16,19,22,22,22,22,22,22,26,24,26,27,27,27,26,26,26,26,27,27,27,29,29,29,34,34,34,29,29,29,27,27,29,29,32,32,34,34,37,38,37,35,35,34,35,38,38,40,40,40,48,48,46,46,56,56,58,69,69,83};
    // defIntra above is listed in zigzag order (ISO 13818-2 default_intra_quantiser_matrix scanned)
    uint8_t m[128]; memcpy(m,defIntra,64); memset(m+64,16,64);
    memcpy(&ram[0x300000],m,128);
    svr06ipu::write32(0x10002000, 0); // BCLR bp0
    c4[0]=0; c4[1]=0x300000; c4[2]=8; c4[0]=0x101; svr06ipu::dmaStart(4);
    svr06ipu::write32(0x10002000, 5u<<28);            // SETIQ intra
    svr06ipu::write32(0x10002000, (5u<<28)|(1u<<27)); // SETIQ non-intra
    printf("setiq ctrl=%08X\n", svr06ipu::read32(0x10002010));
  }
  // slices
  std::vector<size_t> sl;
  for(size_t i=0;i+4<es.size();i++) if(es[i]==0&&es[i+1]==0&&es[i+2]==1&&es[i+3]>=1&&es[i+3]<=0xAF) sl.push_back(i);
  printf("slices=%zu\n",sl.size());
  int mbDone=0;
  for(size_t s=0;s<sl.size();s++){
    size_t a=sl[s], b=(s+1<sl.size())?sl[s+1]:es.size();
    // include following start code (or 00 00 01 B7 end) so IDEC sees the terminating start code
    std::vector<uint8_t> buf(es.begin()+a, es.begin()+std::min(es.size(), b+4));
    if(b>=es.size()){ uint8_t e[4]={0,0,1,0xB7}; buf.insert(buf.end(),e,e+4);} 
    while(buf.size()%16) buf.push_back(0);
    buf.resize(buf.size()+32,0);
    memcpy(&ram[0x100000],buf.data(),buf.size());
    int vpos=es[a+3]-1;
    int qsc=es[a+4]>>3;
    int mbInSlice=MBW*MBH;
    uint8_t *outp=&ram[0x200000]; memset(outp,0xEE,1024*mbInSlice);
    svr06ipu::write32(0x10002000, 0u<<28 | 32); // BCLR, bp=32 (skip start code)
    c3[0]=0; c3[1]=0x200000; c3[2]=64*mbInSlice; c3[0]=0x100; svr06ipu::dmaStart(3);
    c4[0]=0; c4[1]=0x100000; c4[2]=buf.size()/16; c4[0]=0x101; svr06ipu::dmaStart(4);
    svr06ipu::write32(0x10002000, (1u<<28) | (qsc<<16) | 7);
    int spins=0; while((svr06ipu::read32(0x10002010)>>31) && spins<1000) spins++;
    uint32_t ctrl=svr06ipu::read32(0x10002010);
    printf("slice %zu vpos=%d qsc=%d ctrl=%08X spins=%d ch3qwc_left=%u ch3str=%u\n",s,vpos,qsc,ctrl,spins,c3[2],(c3[0]>>8)&1);
    int produced=(64*mbInSlice-(int)c3[2])/64;
    static int mbPos=-1; if(mbPos<0) mbPos=0; int first=vpos*MBW; if(mbPos<first) mbPos=first;
    printf("   produced=%d MBs starting at mb %d\n",produced,mbPos);
    for(int m=0;m<produced;m++){
      int mx=(mbPos+m)%MBW, my=(mbPos+m)/MBW; if(my>=MBH) break;
      for(int y=0;y<16;y++)for(int x=0;x<16;x++){
        memcpy(&rgb[((my*16+y)*W+mx*16+x)*4], outp+m*1024+(y*16+x)*4, 4);
      }
    }
    mbPos+=produced; mbDone+=produced;
  }
  // reference: ffmpeg yuv -> IPU CSC formula
  std::ifstream r("ref.yuv",std::ios::binary); std::vector<uint8_t> yuv((std::istreambuf_iterator<char>(r)),{});
  const uint8_t *Y=yuv.data(), *U=Y+W*H, *V=U+W*H/4;
  long maxd=0, sum=0; int n=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
    int lum=(0x95*std::max(0,(int)Y[y*W+x]-16))>>6, cr=V[(y/2)*(W/2)+x/2]-128, cb=U[(y/2)*(W/2)+x/2]-128;
    int R=std::clamp((lum+((0xcc*cr)>>6)+1)>>1,0,255), G=std::clamp((lum+((-0x68*cr)>>6)+((-0x32*cb)>>6)+1)>>1,0,255), B=std::clamp((lum+((0x102*cb)>>6)+1)>>1,0,255);
    const uint8_t*p=&rgb[(y*W+x)*4]; int d=std::max({abs(p[0]-R),abs(p[1]-G),abs(p[2]-B)}); maxd=std::max<long>(maxd,d); sum+=d; n++;
  }
  { const uint8_t *Y=yuv.data(), *U=Y+W*H, *V=U+W*H/4; double dy=0,du=0,dv=0;
    for(size_t k=0;k<mbs.size()&&k<(size_t)(MBW*MBH);k++){ int mx=k%MBW,my=k/MBW; const uint8_t*m=mbs[k].data();
      for(int y=0;y<16;y++)for(int x=0;x<16;x++) dy+=abs(m[y*16+x]-Y[(my*16+y)*W+mx*16+x]);
      for(int y=0;y<8;y++)for(int x=0;x<8;x++){ du+=abs(m[256+y*8+x]-U[(my*8+y)*(W/2)+mx*8+x]); dv+=abs(m[320+y*8+x]-V[(my*8+y)*(W/2)+mx*8+x]); }
      if(k<3) printf("mb%zu: Y0=%d/%d Cb0=%d/%d Cr0=%d/%d\n",k,m[0],Y[(my*16)*W+mx*16],m[256],U[(my*8)*(W/2)+mx*8],m[320],V[(my*8)*(W/2)+mx*8]); }
    printf("plane mean abs diff: Y=%.2f Cb=%.2f Cr=%.2f (mbs=%zu)\n",dy/(W*H),du/(W*H/4),dv/(W*H/4),mbs.size()); }
  printf("compare: maxdiff=%ld meandiff=%.3f  irq3=%d irq4=%d  alpha0=%02X\n",maxd,(double)sum/n,irqs[3],irqs[4],rgb[3]);
  FILE*o=fopen("out.ppm","wb"); fprintf(o,"P6 %d %d 255\n",W,H); for(int i=0;i<W*H;i++) fwrite(&rgb[i*4],1,3,o); fclose(o);
}
