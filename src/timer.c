/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#include "timer.h"
#include <time.h>

#if __APPLE__
#include "log.h"
#include <mach/mach_time.h>
static mach_timebase_info_data_t tb;

void time_init(void) {
    mach_timebase_info(&tb);
    LOG_DEBUG("tb.numer: %u, tb.denom: %u", tb.numer, tb.denom);
}
#else
void time_init(void) {}
#endif

uint64_t now_ns(void) {
#if __APPLE__
    return mach_absolute_time() * tb.numer / tb.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#endif
}

void sleep_until_ns(uint64_t deadline) {
    for (;;) {
        uint64_t now = now_ns();
        if (now >= deadline)
            return;

        uint64_t delta = deadline - now;
        struct timespec ts = {
            .tv_sec = delta / 1000000000ull,
            .tv_nsec = delta % 1000000000ull,
        };
        nanosleep(&ts, NULL);
    }
}
