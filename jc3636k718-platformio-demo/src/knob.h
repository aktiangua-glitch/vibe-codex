#ifndef JC3636K718_KNOB_BRIDGE_H
#define JC3636K718_KNOB_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include "bidi_switch_knob.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called by the low-level encoder timer in scr_st77916.h.
void knob_change(knob_event_t event, int raw_count);

// Consumed by the application/UI task.
int32_t knob_take_delta(void);
int32_t knob_get_total(void);
bool knob_has_moved(void);

#ifdef __cplusplus
}
#endif

#endif
