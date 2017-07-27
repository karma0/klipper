// Generic interrupt based timer helper functions
//
// Copyright (C) 2017  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_FREQ
#include "board/irq.h" // irq_disable
#include "board/misc.h" // timer_from_us
#include "board/timer_irq.h" // timer_dispatch_many
#include "basecmd.h" // stats_note_sleep
#include "command.h" // shutdown
#include "sched.h" // sched_timer_dispatch

DECL_CONSTANT(CLOCK_FREQ, CONFIG_CLOCK_FREQ);

// Return the number of clock ticks for a given number of microseconds
uint32_t
timer_from_us(uint32_t us)
{
    return us * (CONFIG_CLOCK_FREQ / 1000000);
}

// Return true if time1 is before time2.  Always use this function to
// compare times as regular C comparisons can fail if the counter
// rolls over.
uint8_t
timer_is_before(uint32_t time1, uint32_t time2)
{
    return (int32_t)(time1 - time2) < 0;
}

// Called by main code once every millisecond.  (IRQs disabled.)
void
timer_periodic(void)
{
}

static uint32_t timer_repeat_until;
#define TIMER_IDLE_REPEAT_TICKS timer_from_us(500)
#define TIMER_REPEAT_TICKS timer_from_us(100)

#define TIMER_MIN_TRY_TICKS timer_from_us(1)
#define TIMER_DEFER_REPEAT_TICKS timer_from_us(5)

// Reschedule timers after a brief pause to prevent task starvation
static uint32_t noinline
force_defer(uint32_t next)
{
    uint32_t now = timer_read_time();
    if (timer_is_before(next + timer_from_us(1000), now))
        shutdown("Rescheduled timer in the past");
    timer_repeat_until = now + TIMER_REPEAT_TICKS;
    return now + TIMER_DEFER_REPEAT_TICKS;
}

// Invoke timers - called from board irq code.
uint32_t
timer_dispatch_many(void)
{
    uint32_t tru = timer_repeat_until;
    for (;;) {
        // Run the next software timer
        uint32_t next = sched_timer_dispatch();

        uint32_t now = timer_read_time();
        int32_t diff = next - now;
        if (diff > (int32_t)TIMER_MIN_TRY_TICKS)
            // Schedule next timer normally.
            return next;

        if (unlikely(timer_is_before(tru, now)))
            // Too many repeat timers from a single interrupt - force a pause
            return force_defer(next);

        // Next timer in the past or near future - wait for it to be ready
        irq_enable();
        while (unlikely(diff > 0))
            diff = next - timer_read_time();
        irq_disable();
    }
}

// Periodic background task that temporarily boosts priority of
// timers.  This helps prioritize timers when tasks are idling.
void
timer_task(void)
{
    static uint32_t last_timer;
    uint32_t lst = last_timer;
    irq_disable();
    uint32_t next = timer_get_next(), cur = timer_read_time();
    if (lst != next) {
        timer_repeat_until = cur + TIMER_IDLE_REPEAT_TICKS;
        irq_enable();
        last_timer = next;
        return;
    }

    // Sleep the processor
    irq_wait();
    uint32_t post_sleep = timer_read_time();
    timer_repeat_until = post_sleep + TIMER_IDLE_REPEAT_TICKS;
    irq_enable();
    stats_note_sleep(post_sleep - cur);
}
DECL_TASK(timer_task);

void
timer_irq_shutdown(void)
{
    timer_repeat_until = timer_read_time() + TIMER_IDLE_REPEAT_TICKS;
}
DECL_SHUTDOWN(timer_irq_shutdown);
