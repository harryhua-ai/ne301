#ifndef STUB_DBG_H
#define STUB_DBG_H
#include <stdio.h>
#define LOG_SVC_INFO(...)  do{ if(test_verbose) printf("[I] " __VA_ARGS__), printf("\n"); }while(0)
#define LOG_SVC_WARN(...)  do{ if(test_verbose) printf("[W] " __VA_ARGS__), printf("\n"); }while(0)
#define LOG_SVC_ERROR(...) do{ if(test_verbose) printf("[E] " __VA_ARGS__), printf("\n"); }while(0)
extern int test_verbose;
#endif
