// Greedy crash-avoiding driver on the native engine: produces a complete, crash-free run of a track (any speed)
// to serve as a fidelity-gate reference once replayed on the real emulator. Seeds from a RAM dump frame
// (e.g. a throttle-A run's frame 0 = race start). Default policy: B on speed-update frames on the ground,
// nothing in the air; each frame, the first candidate whose lookahead (default policy) survives H frames is taken.
//   safedrive <dump.ram> <seedFrame> <maxFrames> <out.sol> [H=120] [laneMoves: e.g. "40U,56U,72U"]
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
using namespace excitebike;
static std::vector<uint8_t> readFile(const char* p){ FILE* f=fopen(p,"rb"); if(!f){perror(p);exit(1);} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); std::vector<uint8_t> d(n); if(fread(d.data(),1,n,f)!=(size_t)n) exit(1); fclose(f); return d; }
static std::string solLine(uint8_t v){ char s[14]="|..|........|"; if(v&8)s[4]='U'; if(v&4)s[5]='D'; if(v&2)s[6]='L'; if(v&1)s[7]='R'; if(v&0x40)s[10]='B'; if(v&0x80)s[11]='A'; return s; }
static void seed(Engine& e, const std::vector<uint8_t>& D, int N){ Engine::State s{}; const uint8_t* r=D.data()+(size_t)N*2048;
  uint32_t cum=0; int bt=0; uint8_t p4E=D[0x4E], pE0=D[0xE0]; for(int i=1;i<=N;i++){ const uint8_t* f=D.data()+(size_t)i*2048; if(f[0x4E]!=p4E) bt++; cum+=(f[0xE0]-pE0)&0x3F; p4E=f[0x4E]; pE0=f[0xE0]; }
  for(size_t i=0;i<Engine::kStateAddrCount;i++) s.packed[i]=r[Engine::kStateAddrs[i]]; s.cum=(uint16_t)cum; s.blockXTransitions=(uint8_t)bt; s.prevBlockX=r[0x4E]; s.firstPostHook=0; s.currentStep=N; e.deserialize(s); }
// Default policy: ground -> B on the frame whose advance is the speed update (0x4C==3 now); air -> R on that frame (no
// friction, gravity step skipped), otherwise L while the angle is below 9 (keeps landings inside the ramp window [8,12)
// and the flat crash window [3,12); a flat landing at angle!=6 only wobbles/bounces), else nothing.
static inline uint8_t defaultInput(const Engine& e){ bool upd = e.ram(0x4C)==3; if(e.ram(0xB0)){ if(upd) return 0x01; return (e.ram(0xAC)<9) ? 0x02 : 0x00; } return upd ? 0x40 : 0; } // B pressed on the frame BEFORE the update frame? no: input of frame f is applied in frame f; the update happens when 0x4C becomes 0 during advance -> press when current 0x4C==3
static inline bool dead(const Engine& e){ return e.isFail() || e.ram(0x3E0)!=0; }
static int survive(Engine e, uint8_t first, int H){ e.advance(reverse8(first)); if(dead(e)) return 0; for(int k=1;k<H;k++){ e.advance(reverse8(defaultInput(e))); if(dead(e)) return k; if(e.isWin()) return H; } return H; }
int main(int argc,char**argv){ if(argc<5){ fprintf(stderr,"usage: safedrive <dump.ram> <seedFrame> <maxFrames> <out.sol> [H] [laneMoves]\n"); return 1; }
  auto D=readFile(argv[1]); int N=atoi(argv[2]), maxF=atoi(argv[3]); int H=argc>5?atoi(argv[5]):120; std::string lm=argc>6?argv[6]:"";
  Engine e; seed(e,D,N); std::vector<uint8_t> out;
  const uint8_t cands[]={0x00,0x40,0x01,0x02,0x41,0x42,0x08,0x04,0x48,0x44};
  for(int f=0; f<maxF && !e.isWin(); f++){
    uint8_t d=defaultInput(e);
    // lane moves "40U,56U,72U": press U/D at the given frames (in addition to the default input)
    { size_t pos=0; while(pos<lm.size()){ size_t c=lm.find(',',pos); std::string tok=lm.substr(pos, c==std::string::npos? std::string::npos : c-pos); if(!tok.empty()){ int fr=atoi(tok.c_str()); char dir=tok.back(); if(fr==f) d|= (dir=='U')?0x08:(dir=='D')?0x04:0; } if(c==std::string::npos) break; pos=c+1; } }
    int best=-1; uint8_t bestIn=d; int bs=survive(e,d,H);
    if(bs<H){ best=bs; for(uint8_t c: cands){ if(c==d) continue; int s=survive(e,c,H); if(s>best){ best=s; bestIn=c; if(s>=H) break; } } }
    e.advance(reverse8(bestIn)); out.push_back(bestIn);
    if(dead(e)){ printf("died at f%d (crash=%u stall=%u)\n",f,e.ram(0x98),e.ram(0x3E0)); break; } }
  FILE* o=fopen(argv[4],"w"); for(uint8_t v: out) fprintf(o,"%s\n",solLine(v).c_str()); fclose(o);
  printf("frames=%zu win=%d cum=%u velX=%u posX=%.1f temp=%u\n",out.size(),e.isWin(),e.cumColumn(),e.velX16(),e.bikePosX(),e.ram(0x3B6)); return 0; }
