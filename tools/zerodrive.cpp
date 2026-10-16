// Zero-loss greedy driver on the native engine: like safedrive, but a candidate "survives" only if, over the lookahead
// horizon with the default policy, there is no crash, no stall and NO speed decrease (coast/hit/mud/wobble). It presses
// lane keys only when needed (loss ahead) or to reach a preferred lane, and honours avoid-rules from emulator
// observations ("f1-f2:lo-hi" = never have 0xB8 in [lo,hi] during frames [f1,f2]).
//   zerodrive <dump.ram> <seedFrame> <maxFrames> <out.sol> [H=120] [preferB8=50] [avoid="f1-f2:lo-hi;..."] [stopCum=0]
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
struct Rule { int f1,f2,lo,hi; }; static std::vector<Rule> g_rules; static int g_prefB8=50;
static inline uint8_t defaultInput(const Engine& e){ bool upd = e.ram(0x4C)==3; if(e.ram(0xB0)){ if(upd) return 0x01; return (e.ram(0xAC)<9) ? 0x02 : 0x00; } return upd ? 0x40 : 0; }
static inline bool violates(const Engine& e, int f){ for(const Rule& r: g_rules) if(f>=r.f1 && f<=r.f2 && e.ram(0xB8)>=r.lo && e.ram(0xB8)<=r.hi) return true; return false; }
static inline bool dead(const Engine& e, int f){ return e.isFail() || e.ram(0x3E0)!=0 || violates(e,f); }
// frames survived (no crash/stall/rule violation/speed loss) over H with the default policy after `first`
static int survive(Engine e, uint8_t first, int f, int H){ unsigned v0=e.velX16(); e.advance(reverse8(first)); if(dead(e,f)) return 0; if(e.velX16()<v0) return 0; unsigned pv=e.velX16();
  for(int k=1;k<H;k++){ e.advance(reverse8(defaultInput(e))); if(dead(e,f+k)) return k; unsigned v=e.velX16(); if(v<pv) return k; pv=v; if(e.isWin()) return H; } return H; }
int main(int argc,char**argv){ if(argc<5){ fprintf(stderr,"usage: zerodrive <dump.ram> <seedFrame> <maxFrames> <out.sol> [H] [preferB8] [avoid] [stopCum]\n"); return 1; }
  auto D=readFile(argv[1]); int N=atoi(argv[2]), maxF=atoi(argv[3]); int H=argc>5?atoi(argv[5]):120; g_prefB8=argc>6?atoi(argv[6]):50; std::string av=argc>7?argv[7]:""; int stopCum=argc>8?atoi(argv[8]):0;
  { size_t p=0; while(p<av.size()){ size_t q=av.find(';',p); std::string t=av.substr(p, q==std::string::npos? std::string::npos : q-p); Rule r; if(sscanf(t.c_str(),"%d-%d:%d-%d",&r.f1,&r.f2,&r.lo,&r.hi)==4) g_rules.push_back(r); if(q==std::string::npos) break; p=q+1; } }
  Engine e; seed(e,D,N); std::vector<uint8_t> out; int losses=0;
  for(int f=0; f<maxF && !e.isWin(); f++){
    if(stopCum && (int)e.cumColumn()>=stopCum) break;
    uint8_t d=defaultInput(e); bool ground = e.ram(0xB0)==0; int b8=e.ram(0xB8);
    std::vector<uint8_t> cands; cands.push_back(d);
    if(ground && b8<g_prefB8) cands.push_back(d|0x08);            // move toward the preferred lane when it costs nothing
    if(ground && b8>g_prefB8) cands.push_back(d|0x04);
    for(uint8_t c: {(uint8_t)(d|0x08),(uint8_t)(d|0x04),(uint8_t)(d&~0x40),(uint8_t)0x40,(uint8_t)0x48,(uint8_t)0x44,(uint8_t)0x01,(uint8_t)0x02,(uint8_t)0x41,(uint8_t)0x42,(uint8_t)0x00}) cands.push_back(c);
    int best=-1; uint8_t bestIn=d;
    for(size_t i=0;i<cands.size();i++){ int s=survive(e,cands[i],f,H); if(s>best){ best=s; bestIn=cands[i]; if(s>=H && i>0) break; if(s>=H) break; } }
    unsigned v0=e.velX16(); e.advance(reverse8(bestIn)); out.push_back(bestIn);
    if(e.velX16()<v0) losses++;
    if(dead(e,f)){ printf("died at f%d (crash=%u stall=%u rule=%d)\n",f,e.ram(0x98),e.ram(0x3E0),violates(e,f)); break; } }
  FILE* o=fopen(argv[4],"w"); for(uint8_t v: out) fprintf(o,"%s\n",solLine(v).c_str()); fclose(o);
  printf("frames=%zu win=%d cum=%u velX=%u posX=%.1f b8=%u speedLosses=%d\n",out.size(),e.isWin(),e.cumColumn(),e.velX16(),e.bikePosX(),e.ram(0xB8),losses); return 0; }
