/* Host-test stub: cooperative single-thread CMSIS-RTOS2 emulation */
#ifndef STUB_CMSIS_OS2_H
#define STUB_CMSIS_OS2_H
#include <stdint.h>
#include <setjmp.h>
typedef void *osThreadId_t;
typedef void *osEventFlagsId_t;
typedef void *osMutexId_t;
typedef struct { const char *name; void *stack_mem; uint32_t stack_size; int32_t priority; } osThreadAttr_t;
#define osPriorityBelowNormal 40
#define osOK 0
#define osFlagsErrorTimeout 0xFFFFFFFEu
#define osFlagsWaitAny 0
extern void harness_yield(void);   /* longjmp back to the harness when idle */
extern uint32_t harness_pending;   /* pending event flags */
extern uint32_t harness_wait_calls;
static inline osEventFlagsId_t osEventFlagsNew(const void *a){(void)a;return (void*)1;}
static inline uint32_t osEventFlagsSet(osEventFlagsId_t f,uint32_t fl){(void)f;harness_pending|=fl;return fl;}
static inline uint32_t osEventFlagsWait(osEventFlagsId_t f,uint32_t want,uint32_t opt,uint32_t to){
    (void)f;(void)opt;(void)to; harness_wait_calls++;
    if(harness_pending & want){ uint32_t got=harness_pending & want; harness_pending&=~got; return got;}
    harness_yield(); return osFlagsErrorTimeout;
}
static inline osMutexId_t osMutexNew(const void *a){(void)a;return (void*)1;}
static inline int osMutexAcquire(osMutexId_t m,uint32_t to){(void)m;(void)to;return osOK;}
static inline int osMutexRelease(osMutexId_t m){(void)m;return osOK;}
static inline int osMutexDelete(osMutexId_t m){(void)m;return osOK;}
static inline uint32_t osEventFlagsClear(osEventFlagsId_t f, uint32_t fl){(void)f; harness_pending &= ~fl; return fl;}
static inline int osEventFlagsDelete(osEventFlagsId_t f){(void)f;return osOK;}
extern osThreadId_t harness_osThreadNew_hook(void(*fn)(void*),void *arg,const void *a);
static inline osThreadId_t osThreadNew(void(*fn)(void*),void *arg,const osThreadAttr_t *a){ return harness_osThreadNew_hook(fn,arg,a); }
static inline int osThreadJoin(osThreadId_t t){(void)t;return osOK;}
extern uint32_t harness_delay_calls;
static inline void osDelay(uint32_t ms){(void)ms;harness_delay_calls++;}
#endif
