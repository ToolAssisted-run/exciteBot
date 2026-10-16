// Trajectory driver: seed from tas.ram at frame N (optionally shift the game cycle 0x4C via CYC env),
// replay reference inputs with per-frame overrides "f:INP" (INP tokens like R, L, RB, -, U, DL...), print the arc.
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <map>
using namespace excitebike;
static std::vector<uint8_t> readFile(const char* p){ FILE* f=fopen(p,"rb"); if(!f){perror(p);exit(1);} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); std::vector<uint8_t> d(n); fread(d.data(),1,n,f); fclose(f); return d; }
static uint8_t parseInput(const std::string& s){ uint8_t v=0; if(s[4]=='U')v|=0x08; if(s[5]=='D')v|=0x04; if(s[6]=='L')v|=0x02; if(s[7]=='R')v|=0x01; if(s[10]=='B')v|=0x40; if(s[11]=='A')v|=0x80; return v; }
static uint8_t tok(const std::string& s){ uint8_t v=0; for(char c: s){ if(c=='U')v|=8; if(c=='D')v|=4; if(c=='L')v|=2; if(c=='R')v|=1; if(c=='B')v|=0x40; if(c=='A')v|=0x80; } return v; }
static std::string inpStr(uint8_t v){ std::string s; if(v&8)s+='U'; if(v&4)s+='D'; if(v&2)s+='L'; if(v&1)s+='R'; if(v&0x40)s+='B'; if(v&0x80)s+='A'; return s.empty()?"-":s; }
struct Dump { std::vector<uint8_t> d; int n; std::vector<uint32_t> cum; std::vector<uint8_t> bt; std::vector<float> posX;
  const uint8_t* f(int i) const { return d.data()+ (size_t)i*2048; }
  void build(){ n=d.size()/2048; cum.assign(n,0); bt.assign(n,0); posX.assign(n,0); uint32_t c=0; int b=0; uint8_t p4E=f(0)[0x4E], pE0=f(0)[0xE0];
    for(int i=0;i<n;i++){ if(i>0){ if(f(i)[0x4E]!=p4E)b++; uint8_t e=f(i)[0xE0]; if(e!=pE0)c+=(e-pE0)&0x3F; p4E=f(i)[0x4E]; pE0=e;} cum[i]=c; bt[i]=b; posX[i]=b*256.0f+f(i)[0x50]+f(i)[0x394]/256.0f; } } };
static void seedAt(Engine& e, const Dump& D, int N, int cyc){ Engine::State s; const uint8_t* r=D.f(N); uint8_t ram[2048]; memcpy(ram,r,2048);
  ram[0x4C]=(uint8_t)((ram[0x4C]+cyc)&3);
  for(size_t i=0;i<Engine::kStateAddrCount;i++){ s.packed[i]=ram[Engine::kStateAddrs[i]]; } s.cum=D.cum[N]; s.blockXTransitions=D.bt[N]; s.prevBlockX=ram[0x4E]; s.firstPostHook=0; s.currentStep=N; e.deserialize(s); }
int main(int argc,char**argv){
  if(argc<5){ fprintf(stderr,"usage: traj <ram> <sol> <start> <end> [f:INP ...]\n"); return 1; }
  Dump D; D.d=readFile(argv[1]); D.build();
  std::vector<uint8_t> inputs; { FILE* f=fopen(argv[2],"r"); char line[64]; while(fgets(line,64,f)){ std::string s(line); if(s.size()>=12) inputs.push_back(parseInput(s)); } fclose(f); }
  int start=atoi(argv[3]), end=atoi(argv[4]); int cyc=getenv("CYC")?atoi(getenv("CYC")):0; int quiet=getenv("QUIET")?atoi(getenv("QUIET")):0;
  std::map<int,uint8_t> ov; for(int i=5;i<argc;i++){ std::string a=argv[i]; size_t p=a.find(':'); if(p==std::string::npos) continue; int f1=atoi(a.substr(0,p).c_str()); std::string t=a.substr(p+1); int f2=f1; size_t q=t.find('-'); 
    // range syntax f1-f2:INP
    if(a.find('-')<p){ f2=atoi(a.substr(a.find('-')+1,p-a.find('-')-1).c_str()); f1=atoi(a.substr(0,a.find('-')).c_str()); }
    for(int f=f1; f<=f2; f++) ov[f]=tok(t); }
  Engine e; seedAt(e,D,start,cyc);
  if(!quiet) printf("f     in   cyc air velX  posX      (ref dx)  posY gndY  bc  sec ang t26 b8  384 380  cum\n");
  int landed=-1, boosted=-1; unsigned pv=e.velX16(); int gap=-999, gapF=-1, gapAng=-1;
  for(int f=start+1; f<=end; f++){ uint8_t in = ov.count(f)? ov[f] : inputs[f-1]; e.advance(reverse8(in)); unsigned v=e.velX16();
    int gndY = 0xA0 - e.ram(0xBC) - e.ram(0xB8);
    if(!quiet) printf("%4d %-4s  %d   %d %5u %9.3f (%+7.3f) %4u %4d %3u %3u %3u %3u %3u  %3u %3u %4u%s%s\n", f, inpStr(in).c_str(), e.ram(0x4C), e.ram(0xB0), v, e.bikePosX(), e.bikePosX()-D.posX[f], e.ram(0x8C), gndY, e.ram(0xBC), e.ram(0x58), e.ram(0xAC), e.ram(0x26), e.ram(0xB8), e.ram(0x384), e.ram(0x380), e.cumColumn(), e.isFail()?" CRASH":"", e.ram(0x374)?" WOBBLE":"");
    if(e.ram(0xB0)==2 && e.ram(0x58)==0x14){ int g=(int)e.ram(0x8C)-gndY; if(g>gap){gap=g; gapF=f; gapAng=e.ram(0xAC);} }
    if(e.ram(0xB0)==0 && landed<0 && f>start+2) landed=f; if(v>pv+200 && boosted<0) boosted=f; pv=v; if(e.isFail()) { printf("CRASH at f%d\n",f); break; } }
  printf("summary: landed=%d boosted=%d end velX=%u posX=%.3f (ref %+.3f) cum=%u air=%u gap=%d gapF=%d gapAng=%d\n", landed, boosted, e.velX16(), e.bikePosX(), e.bikePosX()-D.posX[end], e.cumColumn(), e.ram(0xB0), gap, gapF, gapAng);
  return 0; }
