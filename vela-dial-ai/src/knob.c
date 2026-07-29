#include "knob.h"

static volatile int32_t s_pending_delta = 0;
static volatile int32_t s_total = 0;
static volatile bool s_has_moved = false;

void knob_change(knob_event_t event, int raw_count)
{
    const int32_t step = (event == KNOB_RIGHT) ? 1 : -1;
    __atomic_fetch_add(&s_pending_delta, step, __ATOMIC_RELAXED);
    __atomic_store_n(&s_total, raw_count, __ATOMIC_RELAXED);
    __atomic_store_n(&s_has_moved, true, __ATOMIC_RELAXED);
}

int32_t knob_take_delta(void)
{
    return __atomic_exchange_n(&s_pending_delta, 0, __ATOMIC_ACQ_REL);
}

int32_t knob_get_total(void)
{
    return __atomic_load_n(&s_total, __ATOMIC_RELAXED);
}

bool knob_has_moved(void)
{
    return __atomic_load_n(&s_has_moved, __ATOMIC_RELAXED);
}
