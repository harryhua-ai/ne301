#ifndef _COMMON_UTILS
#define _COMMON_UTILS

#define ALIGN_32 __attribute__ ((aligned (32)))
#define IN_PSRAM __attribute__ ((section (".psram_bss")))
#define UNCACHED __attribute__ ((section (".uncached_bss")))
#define SRAM_POOL __attribute__ ((section (".srampool_bss")))

#define WEAK __weak

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define ARRAY_NB(a) (sizeof(a)/sizeof(a[0]))

#endif