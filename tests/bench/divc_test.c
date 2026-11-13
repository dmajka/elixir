/* Elixir 1.4: dsp.c's div10, div100, div2k (a copy: keep them the same) against C's / for every int32_t.
 *   cc -O2 -I firmware/src tests/bench/divc_test.c -o build/divc_test && build/divc_test */
#include <stdio.h>
#include <stdint.h>
static inline int32_t div10(int32_t x) { return (int32_t)(((int64_t)x * 1717986919) >> 34) - (x >> 31); }
static inline int32_t div100(int32_t x) { return (int32_t)(((int64_t)x * 1374389535) >> 37) - (x >> 31); }
static inline int32_t div2k(int32_t x, int k) { return (x + (int32_t)((uint32_t)(x >> 31) >> (32 - k))) >> k; }
int main(void)
{
    uint64_t bad = 0;
    for (int64_t x = INT32_MIN; x <= INT32_MAX; x++)
        bad += div10((int32_t)x) != (int32_t)x / 10 || div100((int32_t)x) != (int32_t)x / 100 ||
               div2k((int32_t)x, 2) != (int32_t)x / 4 || div2k((int32_t)x, 4) != (int32_t)x / 16 ||
               div2k((int32_t)x, 6) != (int32_t)x / 64;
    printf("%s (%llu wrong)\n", bad ? "FAIL" : "ok: all 2^32", (unsigned long long)bad);
    return bad != 0;
}
