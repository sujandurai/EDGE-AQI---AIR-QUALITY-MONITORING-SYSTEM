/* Edge Impulse porting layer for PIC32CM (bare-metal, XC32)
 * Provides: ei_malloc, ei_free, ei_calloc, ei_printf, ei_printf_float,
 *           ei_read_timer_ms, ei_read_timer_us, ei_sleep, ei_putchar, ei_getchar
 */

#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple tick counter - incremented by SysTick or a software loop
static volatile uint64_t ei_tick_ms = 0;

EI_IMPULSE_ERROR ei_run_impulse_check_canceled() {
    return EI_IMPULSE_OK;
}

EI_IMPULSE_ERROR ei_sleep(int32_t time_ms) {
    // Simple busy-wait delay (bare metal)
    volatile uint32_t count = (uint32_t)time_ms * 6000U; // approximate at 48MHz
    while (count--) {
        __asm__ volatile("nop");
    }
    return EI_IMPULSE_OK;
}

uint64_t ei_read_timer_ms() {
    // Return a rough millisecond counter.
    // For timing accuracy, you could hook SysTick here.
    // Returning 0 is valid - inference still works, just no timing info.
    return 0;
}

uint64_t ei_read_timer_us() {
    return 0;
}

void ei_printf(const char *format, ...) {
    // Edge Impulse debug prints - we silently discard them
    // to avoid conflicts with your SERCOM3 UART.
    // If you want them, implement vsnprintf + SERCOM3 write here.
    (void)format;
}

void ei_printf_float(float f) {
    (void)f;
}

void ei_putchar(char c) {
    (void)c;
}

char ei_getchar(void) {
    return 0;
}

void ei_serial_set_baudrate(int baudrate) {
    (void)baudrate;
}

void *ei_malloc(size_t size) {
    return malloc(size);
}

void *ei_calloc(size_t nitems, size_t size) {
    return calloc(nitems, size);
}

void ei_free(void *ptr) {
    free(ptr);
}
