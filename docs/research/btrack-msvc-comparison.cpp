#include "BTrack.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
int main(){for(int hop: {256,512,1024}) for(int mode=0;mode<4;++mode){BTrack t(hop,hop*2);std::uint32_t seed=123456789;for(int i=0;i<3000;++i){seed=seed*1664525u+1013904223u; double noise=(seed>>8)/16777216.0; double time=(double)i*hop/44100.0; double phase=std::fmod(time,60.0/(mode==1?150.0:120.0)); double onset=mode==2?noise*0.3:std::exp(-phase*40.0)+noise*0.01;if(mode==3&&time>8&&time<12)onset=0; t.processOnsetDetectionFunctionSample(onset);std::printf("%d %d %d %d %a %a\n",hop,mode,i,(int)t.beatDueInCurrentFrame(),t.getCurrentTempoEstimate(),t.getLatestCumulativeScoreValue());}}}
