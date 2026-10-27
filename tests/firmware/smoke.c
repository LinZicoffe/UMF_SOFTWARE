#include "ftoa.h"
#include <stdint.h>
#include <string.h>

/* 返回出错源码行，额外保留表达式便于运行器定位。 */
const char *sim_failure;
volatile uint32_t sim_initialized = 0x12345678u;
volatile uint32_t sim_zero_initialized;

uint32_t test_smoke_itoa(void)
{
    char buf[16];
    sim_failure = "itoa_local(INT32_MIN) must equal -2147483648";
    if (itoa_local((-2147483647 - 1), buf) != buf) return __LINE__;
    if (strcmp(buf, "-2147483648") != 0) return __LINE__;
    return 0;
}

uint32_t test_runner_initialization(void)
{
    sim_failure = ".data value or .bss zero initialization failed";
    if (sim_initialized != 0x12345678u) return __LINE__;
    if (sim_zero_initialized != 0u) return __LINE__;
    return 0;
}

/* 仅验证运行器拒绝坏程序，不是待测生产逻辑。 */
uint32_t probe_invalid_read(void) { return *(volatile uint32_t *)0x60000000u; }
uint32_t probe_invalid_write(void) { *(volatile uint32_t *)0x60000000u = 1; return 0; }
uint32_t probe_flash_write(void) { *(volatile uint32_t *)0x08002400u = 1; return 0; }
uint32_t probe_invalid_instruction(void) { __asm volatile ("udf #0"); return 0; }
uint32_t probe_loop(void) { for (;;) { __asm volatile ("nop"); } }
__attribute__((naked)) uint32_t probe_bad_stack(void)
{
    __asm volatile ("sub sp, #8\n movs r0, #0\n bx lr");
}
