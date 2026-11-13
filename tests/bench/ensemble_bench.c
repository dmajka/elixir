/* Elixir: ENSEMBLE (1.4): en_block's output hashed over a knob grid (bit-identical check) and its host time.
 *   cc -O2 -w -I build/gen -I firmware/src -I tests tests/bench/ensemble_bench.c -o build/ensemble_bench && build/ensemble_bench 2
 * 1.4: hash 0606f35e149fc9c5 (the template for other voices: tables_unpack() first, or the tables are zeros) */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "engine_host.h"
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
int main(int argc,char**argv)
{
    static const uint8_t W[]={0,10,21,42,55,64,85,100,106,127}, T[]={0,10,21,42,55,64,85,106,127}, SP[]={0,23,64,127},
        DT[]={0,90}, BL[]={0,64,127};
    static const uint32_t INC[]={2000000u, 20000000u, 200000000u, 900000000u};
    voice_t v; int16_t buf[BLOCK]; uint64_t h=1469598103934665603ull, t0, tt=0; uint32_t n=0;
    int reps = argc>1 ? atoi(argv[1]) : 1;
    tables_unpack();
    memset(&v,0,sizeof v);
    for (unsigned a=0;a<sizeof W;a++) for (unsigned b=0;b<sizeof T;b++) for (unsigned sc=0;sc<128;sc+=16)
    for (unsigned c=0;c<sizeof SP;c++) for (unsigned d=0;d<sizeof DT;d++) for (unsigned e=0;e<sizeof BL;e++)
    for (unsigned p=0;p<4;p++) {
        ensemble_t *E=&eno[1];
        memset(E,0,sizeof *E);
        E->on=1; E->warp=W[a]; E->twist=T[b]; E->scale=sc; E->spread=SP[c]; E->det=DT[d]; E->bal=BL[e];
        v.osc.inc=INC[p];
        en_trigger(1);
        t0=now();
        for (int r=0;r<reps;r++) for (int k=0;k<6;k++) {
            en_block(1,&v,buf);
            if (!r) for (int i=0;i<BLOCK;i++){h^=(uint16_t)buf[i];h*=1099511628211ull;}
            if (k==2) E->spread = (uint8_t)(E->spread + 5);   /* (a crossfade moving) */
        }
        tt+=now()-t0; n++;
    }
    printf("hash %016llx  %u cases  %.1f ns/block\n",(unsigned long long)h,n,(double)tt/n/6/reps);
    return 0;
}
