// Exact segment search over the native Excitebike engine (parallel, OpenMP).
//
// Seeds the engine from a reference RAM dump at frame N (optionally shifting velX, position or the
// game-cycle phase), verifies reproduction, and runs an exhaustive layer-by-layer breadth-first
// expansion over a frame window with exact state de-duplication. No heuristics: within the window the
// answer is complete ("no state reaches velX >= X" is a proof for the modelled input alphabet).
//
//   segsearch <tas.ram> <race01.sol> verify <start> <end>
//   segsearch <tas.ram> <race01.sol> replay <start> <end>            (reference inputs, VELX override)
//   segsearch <tas.ram> <race01.sol> bfs    <start> <end> [full|fullA]
//
// Environment:
//   THREADS=n     OpenMP threads (default 1 -- parallelism is opt-in)
//   VELX=v        override the seed velX            POSX=px     shift the seed by whole px (negative = behind)
//   SUBPX=n       additional shift of n/256 px (sub-pixel byte 0x394, carries into POSX)
//   SEEDINPUTS=f  replay the input tokens in file f (frames start+1..) on the seed before searching
//   VELXAFTER=v   set velX to v after the SEEDINPUTS prefix (parametrize the launch speed at a lip)
//   HOLDSPEED=1   freeze the ground speed inside the window (no accel/coast): the launch speed is exactly VELXAFTER
//   CYC=k         shift the seed game cycle 0x4C    LOSSMAX=d   prune states more than d below their own max velX
//                 (end-of-frame values only: a friction hit in the same frame as a +256 lip boost is invisible to
//                 the bound, so with the launch on a speed-update frame LOSSMAX=28 admits a superset of one-hit lines)
//   PXSLACK=px    prune states more than px behind the reference posX at the same frame
//   MASKX=1       drop display-only bytes (0x50/0x4E/0x3BF/0x38C, block transitions) from the dedup key
//   STOPVELX=v    stop as soon as a state reaches velX >= v (prints it)
//   LANDBY=f LANDY=y  sound landing-reachability prune: drop airborne states whose posY cannot reach >= y by
//                 frame f even under the maximum possible descent (accumulator +53 every frame, rises skipped on
//                 every speed-update frame). Use y = the lowest ground the bike can land on (160 - bcMax - b8Max).
//   LANDVMAX=v    states with velX >= v are exempt from the landing prune (they already took the target lip's +256)
//   FORBID="f1-f2:lo-hi;..."  prune states whose lane byte 0xB8 is in [lo,hi] during frames [f1,f2] (QNES-observed opponent)
//   MINPRESS=1    among duplicate states keep the representative with the fewest lane presses (no pointless lane dancing)
//   VELXMAX=v     prune states with velX > v (e.g. exclude the full-speed launch branch of a lip pair question)
//   MAXSTATES=n   abort if a layer exceeds n states (memory guard, default 300M)
//   HISTFILE=path spill per-layer parent links to this file instead of RAM
//
// Alphabet: ground = {none,U,D} x {none,R,L} x {none,B} (fullA adds A and A+B; noLR drops the ground leans); air = {none,R,L} x {none,B}.
// L+R is omitted: in the air it has R's arc skip/tilt/gravity (kD868[3]=52) with none's friction (dominated by R),
// on the ground it is R.
//
// Memory: a layer is kept as per-shard vectors (no concatenation copy); nodes are 68 bytes; the dedup table is an
// open-addressing index table (16 bytes/state). Peak ~ (prev + 1.3*next) * 68 B + 16 B * next.
#include "engine.hpp"
#include <omp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
using namespace excitebike;

static std::vector<uint8_t> readFile(const char* p){ FILE* f=fopen(p,"rb"); if(!f){perror(p);exit(1);} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); std::vector<uint8_t> d(n); if(fread(d.data(),1,n,f)!=(size_t)n){perror(p);exit(1);} fclose(f); return d; }
static uint8_t parseInput(const std::string& s){ uint8_t v=0; if(s[4]=='U')v|=0x08; if(s[5]=='D')v|=0x04; if(s[6]=='L')v|=0x02; if(s[7]=='R')v|=0x01; if(s[10]=='B')v|=0x40; if(s[11]=='A')v|=0x80; return v; }
static std::string inpStr(uint8_t v){ std::string s; if(v&8)s+='U'; if(v&4)s+='D'; if(v&2)s+='L'; if(v&1)s+='R'; if(v&0x40)s+='B'; if(v&0x80)s+='A'; return s.empty()?"-":s; }

struct Dump { std::vector<uint8_t> d; int n=0; std::vector<uint32_t> cum; std::vector<uint8_t> bt; std::vector<float> posX;
  const uint8_t* f(int i) const { return d.data()+ (size_t)i*2048; }
  void build(){ n=(int)(d.size()/2048); cum.assign(n,0); bt.assign(n,0); posX.assign(n,0); uint32_t c=0; int b=0; uint8_t p4E=f(0)[0x4E], pE0=f(0)[0xE0];
    for(int i=0;i<n;i++){ if(i>0){ if(f(i)[0x4E]!=p4E)b++; uint8_t e=f(i)[0xE0]; if(e!=pE0)c+=(e-pE0)&0x3F; p4E=f(i)[0x4E]; pE0=e;} cum[i]=c; bt[i]=(uint8_t)b; posX[i]=b*256.0f+f(i)[0x50]+f(i)[0x394]/256.0f; } } };

// ---- options ----
static int g_velXOverride=-1, g_velXAfter=-1, g_holdSpeed=0, g_posShift=0, g_subPx=0, g_cyc=0, g_lossMax=-1, g_maskX=0, g_stopVelX=0, g_threads=1, g_landBy=-1, g_landY=0, g_landVmax=0;
static float g_pxSlack=-1; static size_t g_maxStates=300000000; static int g_minPress=0; static int g_velXMax=0;   // VELXMAX=v: prune states with velX > v (restrict the launch-speed branch)
struct Phase { int f, minVel, loss, landBy, landY, landVmax; bool hasLand; }; static std::vector<Phase> g_phases;   // PHASES="f:minVel:loss[:landBy:landY:landVmax];..."
static int g_phaseF=-1, g_phaseMinVel=0, g_phaseLoss=-1;   // PHASE="f:minVel:lossMax": after layer f keep only states with velX>=minVel, then use lossMax and the full ground alphabet
static int g_stopWin=0, I52=-1;   // STOPWIN=1: stop at the first frame where a state has the race-over flag 0x52==1 and report only those states
static size_t g_beam=0;   // BEAM=N: when a layer exceeds N states keep a uniform sample of ~N (per-shard truncation; NOT exhaustive)
static int g_preferGround=0;   // PREFERGROUND=1: the final table picks, per velX, the best posX among GROUNDED states when any exist   // MINPRESS=1: among equivalent states keep the one with the fewest lane presses
struct Forbid { int f1,f2,lo,hi; }; static std::vector<Forbid> g_forbid;   // FORBID="f1-f2:lo-hi;..." prune states with b8 in [lo,hi] during frames [f1,f2]

static void seedAt(Engine& e, const Dump& D, int N){ Engine::State s{}; const uint8_t* r=D.f(N);
  uint8_t ram[2048]; memcpy(ram,r,2048); uint32_t cum=D.cum[N]; uint8_t bt=D.bt[N];
  ram[0x4C]=(uint8_t)((ram[0x4C]+g_cyc)&3);
  if(g_subPx){ int sp=ram[0x394]+g_subPx; if(sp>255){ sp-=256; g_posShift++; } if(sp<0){ sp+=256; g_posShift--; } ram[0x394]=(uint8_t)sp; }   // SUBPX: 1/256-px shift of the sub-pixel byte, carrying into the whole-px shift
  if(g_posShift){ int d=g_posShift; int px=ram[0x50]+d; if(px>255){ px-=256; ram[0x4E]^=1; bt++; } if(px<0){ px+=256; ram[0x4E]^=1; bt--; } ram[0x50]=(uint8_t)px;
    int c64=(int)(int8_t)ram[0x64]; c64-=d; while(c64<=0){ c64+=8; cum++; } while(c64>8){ c64-=8; cum--; } ram[0x64]=(uint8_t)c64;   // column phase (valid only without backlog)
    if(ram[0x58]) ram[0xC8]=(uint8_t)(ram[0xC8]+d); }   // inside a feature section the section position moves with the bike; outside, it is reset at entry (0x64-1)
  for(size_t i=0;i<Engine::kStateAddrCount;i++){ uint16_t a=Engine::kStateAddrs[i]; uint8_t v=ram[a]; if(g_velXOverride>=0){ if(a==0x90) v=g_velXOverride&0xFF; if(a==0x94) v=(g_velXOverride>>8)&0xFF; } s.packed[i]=v; }
  s.cum=(uint16_t)cum; s.blockXTransitions=bt; s.prevBlockX=ram[0x4E]; s.firstPostHook=0; s.currentStep=N; e.deserialize(s); }

// ---- compact node + dedup key ----
static constexpr size_t KN = Engine::kStateAddrCount;
#pragma pack(push,1)
struct Node { uint8_t packed[KN]; uint16_t cum; uint8_t bt, pbx; uint32_t parent; uint8_t inp; uint16_t vmax; uint16_t cost; };
struct Link { uint32_t parent; uint8_t inp; };
#pragma pack(pop)
static inline void toState(const Node& n, int frame, Engine::State& s){ s=Engine::State{}; memcpy(s.packed,n.packed,KN); s.cum=n.cum; s.blockXTransitions=n.bt; s.prevBlockX=n.pbx; s.firstPostHook=0; s.currentStep=frame; }
static inline void fromState(const Engine::State& s, Node& n){ memcpy(n.packed,s.packed,KN); n.cum=s.cum; n.bt=s.blockXTransitions; n.pbx=s.prevBlockX; }
// Key built field by field (never memcpy of a padded struct: padding differs between threads' stacks and would
// silently defeat de-duplication).
struct Key { uint8_t b[KN+4]; bool operator==(const Key&o)const{return memcmp(b,o.b,sizeof(b))==0;} };
static int idxOf(uint16_t a){ for(size_t i=0;i<Engine::kStateAddrCount;i++) if(Engine::kStateAddrs[i]==a) return (int)i; return -1; }
static int I5,I6,IB0,I80,I84,I78,I7C,I8C,IBF,I50,I4E,I90,I94,I394,I4C,I8CY,IB8;
static void initIdx(){ I5=idxOf(0x3B5); I6=idxOf(0x3B6); IB0=idxOf(0xB0); I80=idxOf(0x380); I84=idxOf(0x384); I78=idxOf(0x378); I7C=idxOf(0x37C); I8C=idxOf(0x38C); IBF=idxOf(0x3BF); I50=idxOf(0x50); I4E=idxOf(0x4E); I90=idxOf(0x90); I94=idxOf(0x94); I394=idxOf(0x394); I4C=idxOf(0x4C); I8CY=idxOf(0x8C); IB8=idxOf(0xB8); }
static inline void makeKey(Key& k, const Node& n){ memcpy(k.b,n.packed,KN); k.b[KN]=(uint8_t)(n.cum&0xFF); k.b[KN+1]=(uint8_t)(n.cum>>8); k.b[KN+2]=n.bt; k.b[KN+3]=n.pbx;
  k.b[I5]=0; k.b[I6]=0; // temperature never matters inside a short window
  if(g_maskX){ k.b[I8C]=0; k.b[IBF]=0; k.b[I50]=0; k.b[I4E]=0; k.b[KN+2]=0; k.b[KN+3]=0; }
  if(n.packed[IB0]==0){ k.b[I80]=0; k.b[I84]=0; k.b[I78]=0; k.b[I7C]=0; k.b[I8C]=0; } } // grounded: flight accumulators are stale
static inline uint64_t hashKey(const Key& k){ uint64_t h=1469598103934665603ULL; for(size_t i=0;i<sizeof(k.b);i++){ h^=k.b[i]; h*=1099511628211ULL; } h^=h>>29; h*=0xBF58476D1CE4E5B9ULL; h^=h>>32; return h; }
static inline unsigned velOf(const Node& n){ return (unsigned)n.packed[I94]*256u+n.packed[I90]; }
static inline float posOf(const Node& n){ return n.bt*256.0f + n.packed[I50] + n.packed[I394]/256.0f; }
// Sound landing-reachability bound: maximum posY reachable by frame LANDBY from an airborne state at frame f.
// Each arc step adds (0x384 - 0x37C) to posY after the accumulator step; the accumulator can grow by at most 53
// per frame; a rise (negative term) can only be avoided on speed-update frames (R skips the step there).
static inline bool canLand(const Node& n, int f){
  if(g_landBy<0 || n.packed[IB0]!=2) return true;
  if(g_landVmax>0 && (int)velOf(n)>=g_landVmax) return true;   // already boosted by the target lip: a new flight, not subject to the bound
  int y=n.packed[I8CY], acc=n.packed[I80], hi=n.packed[I84], vz=n.packed[I7C], c=n.packed[I4C];
  for(int k=1; f+k<=g_landBy; k++){ acc+=53; if(acc>=256){ acc-=256; hi++; } int term=hi-vz; if(((c+k)&3)==0 && term<0) term=0; y+=term; if(y>=g_landY) return true; }
  return y>=g_landY; }

struct Cand { Node n; uint64_t h; };
// One shard: open-addressing table of (hash fragment, index into out). Owned by one thread at a time.
struct Shard {
  std::vector<Node> out; std::vector<uint64_t> table; size_t count=0;
  unsigned vmax=0; float pxmax=0; size_t air=0;
  static constexpr uint64_t EMPTY=~0ull;
  void reset(size_t expected){ out.clear(); out.shrink_to_fit(); out.reserve(expected+expected/4); count=0; vmax=0; pxmax=0; air=0; size_t cap=1024; while(cap<expected*2) cap<<=1; table.assign(cap,EMPTY); }
  void grow(){ std::vector<uint64_t> nt(table.size()*2,EMPTY); size_t mask=nt.size()-1;
    for(uint64_t s: table){ if(s==EMPTY) continue; Key k; makeKey(k,out[(uint32_t)s]); uint64_t h=hashKey(k); size_t i=h&mask; while(nt[i]!=EMPTY) i=(i+1)&mask; nt[i]=s; }
    table.swap(nt); }
  bool insert(const Key& k, uint64_t h, const Node& n){
    if((count+1)*10 > table.size()*6) grow();
    size_t mask=table.size()-1, i=h&mask; uint32_t frag=(uint32_t)(h>>32);
    for(;;){ uint64_t s=table[i];
      if(s==EMPTY){ table[i]=((uint64_t)frag<<32)|(uint32_t)out.size(); out.push_back(n); count++;
        unsigned v=velOf(n); if(v>vmax) vmax=v; float px=posOf(n); if(px>pxmax) pxmax=px; if(n.packed[IB0]) air++; return true; }
      if((uint32_t)(s>>32)==frag){ Key k2; makeKey(k2,out[(uint32_t)s]); if(k2==k){ if(g_minPress && n.cost<out[(uint32_t)s].cost) out[(uint32_t)s]=n; return false; } }
      i=(i+1)&mask; } }
  void release(){ std::vector<uint64_t>().swap(table); }
};
// A layer = the shards' out vectors + prefix offsets (global index = off[s] + local index).
struct Layer { std::vector<std::vector<Node>> parts; std::vector<size_t> off; size_t size() const { return off.back(); }
  const Node& at(size_t g) const { int s=(int)(std::upper_bound(off.begin(),off.end(),g)-off.begin())-1; return parts[s][g-off[s]]; } };

int main(int argc,char**argv){
  if(argc<6){ fprintf(stderr,"usage: segsearch <ram> <sol> verify|replay|bfs <start> <end> [full|fullA]\n"); return 1; }
  initIdx();
  Dump D; D.d=readFile(argv[1]); D.build();
  std::vector<uint8_t> inputs; { FILE* f=fopen(argv[2],"r"); if(!f){perror(argv[2]);return 1;} char line[64]; while(fgets(line,64,f)){ std::string s(line); if(s.size()>=12) inputs.push_back(parseInput(s)); } fclose(f); }
  std::string mode=argv[3]; int start=atoi(argv[4]); int end=atoi(argv[5]);
  if(getenv("VELX")) g_velXOverride=atoi(getenv("VELX"));
  if(getenv("VELXAFTER")) g_velXAfter=atoi(getenv("VELXAFTER"));   // velX override applied AFTER the SEEDINPUTS prefix
  if(getenv("HOLDSPEED")) g_holdSpeed=atoi(getenv("HOLDSPEED"));   // 1: ground speed frozen in the window (launch speed = exact parameter; loss budget = flight hits only)
  if(getenv("POSX")) g_posShift=atoi(getenv("POSX"));
  if(getenv("SUBPX")) g_subPx=atoi(getenv("SUBPX"));
  if(getenv("CYC")) g_cyc=atoi(getenv("CYC"));
  if(getenv("LOSSMAX")) g_lossMax=atoi(getenv("LOSSMAX"));
  if(getenv("PXSLACK")) g_pxSlack=atof(getenv("PXSLACK"));
  if(getenv("MASKX")) g_maskX=atoi(getenv("MASKX"));
  if(getenv("STOPVELX")) g_stopVelX=atoi(getenv("STOPVELX"));
  if(getenv("PHASE")){ sscanf(getenv("PHASE"),"%d:%d:%d",&g_phaseF,&g_phaseMinVel,&g_phaseLoss); }
  if(g_phaseF>=0) g_phases.push_back({g_phaseF,g_phaseMinVel,g_phaseLoss,0,0,0,false});
  if(getenv("PHASES")){ std::string z=getenv("PHASES"); size_t p=0; while(p<z.size()){ size_t q=z.find(';',p); std::string t=z.substr(p, q==std::string::npos? std::string::npos : q-p); Phase ph{0,0,-1,-1,0,0,false}; int n=sscanf(t.c_str(),"%d:%d:%d:%d:%d:%d",&ph.f,&ph.minVel,&ph.loss,&ph.landBy,&ph.landY,&ph.landVmax); if(n>=3){ ph.hasLand=(n>=6); g_phases.push_back(ph); } if(q==std::string::npos) break; p=q+1; } }
  std::sort(g_phases.begin(),g_phases.end(),[](const Phase&a,const Phase&b){return a.f<b.f;}); if(!g_phases.empty()) g_phaseF=g_phases[0].f;
  if(getenv("STOPWIN")){ g_stopWin=atoi(getenv("STOPWIN")); I52=idxOf(0x52); if(I52<0){ fprintf(stderr,"0x52 not in state\n"); return 1; } }
  if(getenv("BEAM")) g_beam=strtoull(getenv("BEAM"),nullptr,10);
  if(getenv("LANDBY")) g_landBy=atoi(getenv("LANDBY"));
  if(getenv("LANDY")) g_landY=atoi(getenv("LANDY"));
  if(getenv("LANDVMAX")) g_landVmax=atoi(getenv("LANDVMAX"));
  if(getenv("MAXSTATES")) g_maxStates=strtoull(getenv("MAXSTATES"),nullptr,10);
  if(getenv("THREADS")) g_threads=std::max(1,atoi(getenv("THREADS")));
  if(getenv("MINPRESS")) g_minPress=atoi(getenv("MINPRESS"));
  if(getenv("VELXMAX")) g_velXMax=atoi(getenv("VELXMAX"));
  if(getenv("PREFERGROUND")) g_preferGround=atoi(getenv("PREFERGROUND"));
  if(getenv("FORBID")){ std::string z=getenv("FORBID"); size_t p=0; while(p<z.size()){ size_t q=z.find(';',p); std::string t=z.substr(p, q==std::string::npos? std::string::npos : q-p); Forbid fb; if(sscanf(t.c_str(),"%d-%d:%d-%d",&fb.f1,&fb.f2,&fb.lo,&fb.hi)==4) g_forbid.push_back(fb); if(q==std::string::npos) break; p=q+1; } }
  omp_set_num_threads(g_threads);
  // SEEDINPUTS=path: tokens (e.g. "R", "UB", "-") for frames start+1.. ; they are replayed on top of the seed and the
  // search/verify/replay then begins at the frame reached (the seed becomes a searched state, not a dump frame).
  std::vector<uint8_t> seedIn; if(getenv("SEEDINPUTS")){ FILE* f=fopen(getenv("SEEDINPUTS"),"r"); if(!f){perror("SEEDINPUTS");return 1;} char tok[32]; while(fscanf(f,"%31s",tok)==1){ uint8_t v=0; for(char* c=tok;*c;c++){ if(*c=='U')v|=8; if(*c=='D')v|=4; if(*c=='L')v|=2; if(*c=='R')v|=1; if(*c=='B')v|=0x40; if(*c=='A')v|=0x80; } seedIn.push_back(v);} fclose(f); }
  auto applySeedInputs=[&](Engine& e){ for(uint8_t v: seedIn) e.advance(reverse8(v)); if(g_velXAfter>=0){ Engine::State st; e.serialize(st); int i90=idxOf(0x90), i94=idxOf(0x94); st.packed[i90]=g_velXAfter&0xFF; st.packed[i94]=(g_velXAfter>>8)&0xFF; e.deserialize(st); } };

  if(mode=="replay"){ Engine e; seedAt(e,D,start); applySeedInputs(e); start+=(int)seedIn.size(); unsigned pv=e.velX16(); uint8_t pa=e.ram(0xB0);
    printf("replay from f%d with velX=%u (ref %u) cyc+%d\n",start,pv,D.f(start)[0x94]*256+D.f(start)[0x90],g_cyc);
    for(int f=start+1; f<=end && f-1<(int)inputs.size(); f++){ e.advance(reverse8(inputs[f-1])); unsigned v=e.velX16(); uint8_t a=e.ram(0xB0);
      if(v!=pv || a!=pa || e.isFail() || e.ram(0x3E0)) printf("  f%4d: velX %5u->%5u air %u->%u posX=%9.3f (ref %9.3f, %+7.3f) cum=%u (ref %u) ang=%u b8=%u temp=%u%s%s\n", f,pv,v,pa,a,e.bikePosX(),f<D.n?D.posX[f]:0.f,e.bikePosX()-(f<D.n?D.posX[f]:0.f),e.cumColumn(),f<D.n?D.cum[f]:0u,e.ram(0xAC),e.ram(0xB8),e.ram(0x3B6), e.isFail()?" CRASH":"", e.ram(0x3E0)?" STALL":"");
      pv=v; pa=a; if(e.isWin()){ printf("  RACE OVER at f%d (ref 2261)\n",f); break; } if(e.isFail()) break; }
    printf("end: posX=%.3f velX=%u cum=%u\n",e.bikePosX(),e.velX16(),e.cumColumn()); return 0; }
  if(mode=="verify"){ Engine e; seedAt(e,D,start); applySeedInputs(e); start+=(int)seedIn.size(); int bad=0;
    for(int f=start+1; f<=end && f<D.n; f++){ e.advance(reverse8(inputs[f-1])); const uint8_t* r=D.f(f);
      bool ok = e.ram(0x50)==r[0x50] && e.ram(0x394)==r[0x394] && e.ram(0x90)==r[0x90] && e.ram(0x94)==r[0x94] && e.ram(0xB0)==r[0xB0] && e.ram(0x8C)==r[0x8C] && e.ram(0xAC)==r[0xAC] && e.ram(0xB8)==r[0xB8] && e.ram(0x380)==r[0x380] && e.ram(0x3B6)==r[0x3B6] && e.cumColumn()==D.cum[f];
      if(!ok){ if(bad<5) printf("  mismatch at f%d: posX %u/%u sub %u/%u velX %u/%u air %u/%u posY %u/%u cum %u/%u\n",f,e.ram(0x50),r[0x50],e.ram(0x394),r[0x394],e.velX16(),r[0x94]*256+r[0x90],e.ram(0xB0),r[0xB0],e.ram(0x8C),r[0x8C],e.cumColumn(),D.cum[f]); bad++; } }
    printf("verify f%d..f%d: %d mismatching frames; final posX=%.3f velX=%u cum=%u raceOver=%d\n",start,end,bad,e.bikePosX(),e.velX16(),e.cumColumn(),e.isWin()); return 0; }
  if(mode=="verifyall"){ // compare EVERY state address each frame (except temperature sub-counter 0x3B5 and the unserialized 0x5C); print the first few differing (frame, addr)
    Engine e; seedAt(e,D,start); applySeedInputs(e); start+=(int)seedIn.size(); int bad=0; int badFrames=0;
    for(int f=start+1; f<=end && f<D.n; f++){ e.advance(reverse8(inputs[f-1])); const uint8_t* r=D.f(f); bool any=false;
      for(size_t i=0;i<Engine::kStateAddrCount;i++){ uint16_t a=Engine::kStateAddrs[i]; if(a==0x3B5||a==0x5C) continue; if(e.ram(a)!=r[a]){ any=true; if(bad<12) printf("  f%d: 0x%03X engine=%u game=%u\n",f,a,e.ram(a),r[a]); bad++; } }
      if(any) badFrames++; }
    printf("verifyall f%d..f%d: %d differing bytes in %d frames\n",start,end,bad,badFrames); return 0; }
  if(mode!="bfs"){ fprintf(stderr,"unknown mode %s\n",mode.c_str()); return 1; }

  // ---- BFS ----
  std::vector<uint8_t> alphaG, alphaA; std::string A = argc>6? argv[6] : "full";
  std::vector<uint8_t> ud={0,8,4}, lr={0,1,2}, th={0,0x40}; if(A=="fullA") th={0,0x40,0x80,0xC0};
  std::vector<uint8_t> alphaG2; for(auto u:ud)for(auto l:lr)for(auto t:th) alphaG2.push_back(u|l|t);   // ground alphabet after the first phase (leans added)
  if(A=="noLR"){ for(auto u:ud)for(auto t:th) alphaG.push_back(u|t); }      // ground: lane moves + throttle only (no wheelies)
  else for(auto u:ud)for(auto l:lr)for(auto t:th) alphaG.push_back(u|l|t);
  for(auto l:lr)for(auto t:std::vector<uint8_t>{0,0x40}) alphaA.push_back(l|t);
  if(A!="noLR") alphaG2=alphaG;   // only the lean-free alphabet is upgraded after the first phase

  const int T=g_threads; const int S=std::max(64, 8*T);            // shards
  const size_t BLOCK=std::max<size_t>(65536, (size_t)T*32768);     // parents expanded per block
  std::vector<Engine> engines(T); std::vector<Shard> shards(S);
  std::vector<std::vector<std::vector<Cand>>> tbuf(T, std::vector<std::vector<Cand>>(S));
  // history: per layer a Link array (RAM or spilled to HISTFILE)
  FILE* hf=nullptr; std::vector<long> hoff; std::vector<std::vector<Link>> hist;
  if(getenv("HISTFILE")){ hf=fopen(getenv("HISTFILE"),"w+b"); if(!hf){perror("HISTFILE");return 1;} }
  auto pushHist=[&](const Layer& L){ if(hf){ hoff.push_back(ftell(hf)); for(auto& part: L.parts) for(const Node& n: part){ Link l{n.parent,n.inp}; fwrite(&l,sizeof(l),1,hf);} }
    else { std::vector<Link> l; l.reserve(L.size()); for(auto& part: L.parts) for(const Node& n: part) l.push_back({n.parent,n.inp}); hist.push_back(std::move(l)); } };
  auto getLink=[&](int li, uint32_t idx)->Link{ if(hf){ Link l; fseek(hf, hoff[li]+(long)idx*(long)sizeof(Link), SEEK_SET); if(fread(&l,sizeof(l),1,hf)!=1){l.parent=0;l.inp=0;} return l; } return hist[li][idx]; };

  Layer cur; { Engine e; seedAt(e,D,start); applySeedInputs(e); start+=(int)seedIn.size(); Engine::State s; e.serialize(s); Node n{}; fromState(s,n); n.parent=0xFFFFFFFFu; n.inp=0; n.vmax=(uint16_t)e.velX16(); n.cost=0; cur.parts.assign(1,{n}); cur.off={0,1}; pushHist(cur); }
  printf("bfs f%d..f%d threads=%d shards=%d cyc+%d posx%+d lossMax=%d maskX=%d stopVelX=%d landBy=%d landY=%d maxStates=%zu\n",start,end,T,S,g_cyc,g_posShift,g_lossMax,g_maskX,g_stopVelX,g_landBy,g_landY,g_maxStates); fflush(stdout);
  bool won=false;
  double t0=omp_get_wtime();
  for(int f=start+1; f<=end; f++){
    int curLoss=g_lossMax; for(const Phase& ph: g_phases) if(f>ph.f){ curLoss=ph.loss; if(ph.hasLand){ g_landBy=ph.landBy; g_landY=ph.landY; g_landVmax=ph.landVmax; } }
    size_t expected = std::max<size_t>(1024, cur.size()*13/10/(size_t)S + 1);
    for(auto& sh: shards) sh.reset(expected);
    const float refPx = (f<D.n)? D.posX[f] : 1e9f;
    const size_t total=cur.size();
    for(size_t b0=0; b0<total; b0+=BLOCK){ size_t b1=std::min(total, b0+BLOCK);
      #pragma omp parallel
      { int t=omp_get_thread_num(); Engine& e=engines[t]; auto& bufs=tbuf[t]; Engine::State st;
        #pragma omp for schedule(dynamic,512)
        for(size_t g=b0; g<b1; g++){ const Node& p=cur.at(g); const bool air = p.packed[IB0]!=0; const auto& alpha = air? alphaA : ((g_phaseF>=0 && f>g_phaseF)? alphaG2 : alphaG); toState(p,f-1,st);
          for(uint8_t in: alpha){ e.deserialize(st); e.advance(reverse8(in));
            if(e.isFail()) continue; if(e.ram(0x3E0)!=0) continue;                       // crash / overheat stall
            if(g_holdSpeed && !air && e.ram(0xB0)==0 && (e.ram(0x90)!=p.packed[I90] || e.ram(0x94)!=p.packed[I94])){ Engine::State hs; e.serialize(hs); hs.packed[I90]=p.packed[I90]; hs.packed[I94]=p.packed[I94]; e.deserialize(hs); }
            unsigned v=e.velX16(); if(g_velXMax && (int)v>g_velXMax) continue; uint16_t vm=std::max<uint16_t>(p.vmax,(uint16_t)v); { int lm = curLoss; if(lm>=0 && (int)vm-(int)v>lm) continue; }
            if(g_pxSlack>=0 && e.bikePosX() < refPx-g_pxSlack) continue;
            Cand c{}; Engine::State cs; e.serialize(cs); fromState(cs,c.n); c.n.parent=(uint32_t)g; c.n.inp=in; c.n.vmax=vm; c.n.cost=(uint16_t)(p.cost + ((in&0x0C)?1:0));
            if(!canLand(c.n,f)) continue;
            { bool bad=false; for(const Forbid& fb: g_forbid) if(f>=fb.f1 && f<=fb.f2 && c.n.packed[IB8]>=fb.lo && c.n.packed[IB8]<=fb.hi){ bad=true; break; } if(bad) continue; }
            Key k; makeKey(k,c.n); c.h=hashKey(k);
            bufs[(size_t)(c.h % (uint64_t)S)].push_back(c); } }
      }
      #pragma omp parallel for schedule(dynamic,1)
      for(int s=0;s<S;s++){ Shard& sh=shards[s]; for(int t=0;t<T;t++){ auto& b=tbuf[t][s]; for(const Cand& c: b){ Key k; makeKey(k,c.n); sh.insert(k,c.h,c.n); } b.clear(); } }
      size_t tot=0; for(auto& sh: shards) tot+=sh.count;
      if(tot>g_maxStates){ printf("ABORT at f%d: layer exceeds MAXSTATES (%zu > %zu)\n",f,tot,g_maxStates); fflush(stdout); return 2; }
    }
    // the shards' out vectors BECOME the next layer (no copy); release the previous layer and the tables
    Layer next; next.parts.resize(S); next.off.assign(S+1,0);
    for(int s=0;s<S;s++){ next.parts[s].swap(shards[s].out); next.off[s+1]=next.off[s]+next.parts[s].size(); shards[s].release(); }
    if(g_beam && next.size()>g_beam){ size_t per=std::max<size_t>(1,g_beam/(size_t)S); for(int s2=0;s2<S;s2++){ auto& pv=next.parts[s2]; if(pv.size()>per) pv.resize(per); next.off[s2+1]=next.off[s2]+pv.size(); } }
    for(const Phase& ph: g_phases) if(f==ph.f){ size_t before=next.size(); const int mv=ph.minVel; for(int s2=0;s2<S;s2++){ auto& pv=next.parts[s2]; pv.erase(std::remove_if(pv.begin(),pv.end(),[mv](const Node& n){ return (int)velOf(n)<mv; }),pv.end()); next.off[s2+1]=next.off[s2]+pv.size(); } printf("PHASE at f%d: kept %zu of %zu states with velX>=%d; lossMax -> %d%s\n",f,next.size(),before,mv,ph.loss,ph.hasLand?" (new landing bound)":""); fflush(stdout); }
    unsigned vmax=0; float pxmax=0; size_t air=0; for(auto& sh: shards){ vmax=std::max(vmax,sh.vmax); pxmax=std::max(pxmax,sh.pxmax); air+=sh.air; }
    printf("f%4d: states=%9zu  maxVelX=%5u  maxPosX=%9.3f  airborne=%zu  ref: velX=%u posX=%.3f air=%u  [%.1fs]\n", f, next.size(), vmax, pxmax, air, f<D.n?D.f(f)[0x94]*256+D.f(f)[0x90]:0, f<D.n?D.posX[f]:0.f, f<D.n?D.f(f)[0xB0]:0, omp_get_wtime()-t0); fflush(stdout);
    pushHist(next);
    cur=std::move(next);
    if(cur.size()==0){ printf("layer empty at f%d\n",f); end=f; break; }
    if(g_stopWin){ size_t w=0; for(size_t g=0;g<cur.size();g++) if(cur.at(g).packed[I52]==1) w++; if(w){ printf("WIN: %zu states with race over at f%d\n",w,f); fflush(stdout); end=f; won=true; break; } }
    if(g_stopVelX && (int)vmax>=g_stopVelX){ printf("STOP: velX %u >= %d reached at f%d\n",vmax,g_stopVelX,f); end=f; break; }
  }
  // ---- final report: per distinct velX, best posX; input strings for the top rows ----
  std::unordered_map<unsigned,std::pair<float,size_t>> best;
  for(size_t g=0; g<cur.size(); g++){ const Node& n=cur.at(g); if(won && n.packed[I52]!=1) continue; unsigned v=velOf(n); float px=posOf(n); bool gnd = n.packed[IB0]==0;
    auto it=best.find(v);
    if(g_preferGround){ bool curGnd = it!=best.end() && cur.at(it->second.second).packed[IB0]==0;
      if(it==best.end() || (gnd && !curGnd) || (gnd==curGnd && px>it->second.first)) best[v]={px,g}; }
    else if(it==best.end()||px>it->second.first) best[v]={px,g}; }
  std::vector<std::pair<unsigned,std::pair<float,size_t>>> rows(best.begin(),best.end()); std::sort(rows.begin(),rows.end(),[](auto&a,auto&b){return a.first>b.first;});
  const float refEnd = end<D.n? D.posX[end] : 0.f; const unsigned refVel = end<D.n? D.f(end)[0x94]*256+D.f(end)[0x90] : 0;
  printf("\n=== frame %d: best posX per velX (ref posX=%.3f velX=%u) ===\n", end, refEnd, refVel);
  Engine e; int shown=0; int layers=(int)(hf? hoff.size() : hist.size());
  for(auto& r: rows){ if(shown++>=6) break; Engine::State s; toState(cur.at(r.second.second),end,s); e.deserialize(s);
    printf("velX=%5u posX=%9.3f (ref %+8.3f) air=%u ang=%u b8=%u posY=%u temp=%u cum=%u\n", r.first, r.second.first, r.second.first-refEnd, e.ram(0xB0), e.ram(0xAC), e.ram(0xB8), e.ram(0x8C), e.ram(0x3B6), e.cumColumn());
    std::vector<uint8_t> seq; int li=layers-1; uint32_t idx=(uint32_t)r.second.second; while(li>0){ Link l=getLink(li,idx); seq.push_back(l.inp); idx=l.parent; li--; } std::reverse(seq.begin(),seq.end());
    printf("  inputs f%d..f%d:", start+1, end); for(size_t i=0;i<seq.size();i++){ printf(" %s", inpStr(seq[i]).c_str()); if(start+(int)i<(int)inputs.size() && seq[i]!=inputs[start+i]) printf("*"); } printf("\n"); }
  if(hf) fclose(hf);
  return 0; }
