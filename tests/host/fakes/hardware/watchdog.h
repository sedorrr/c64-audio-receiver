#pragma once
#include <stdlib.h>
static inline void watchdog_update(void){}
extern int REBOOTED; 
#include <setjmp.h>
extern jmp_buf REBOOT_JMP;
static inline void watchdog_reboot(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c; REBOOTED=1; longjmp(REBOOT_JMP,1);}
