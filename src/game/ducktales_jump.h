/*
 * ducktales_jump.h - reversible jump-height experiment.
 *
 * Scales Scrooge's take-off velocity by a percentage, as a handler of
 * EVENT_JUMP_START (ducktales/events.h). 100 is the stock game: the handler
 * is then unregistered and the event's site hook disabled, so the machine
 * runs exactly as without this code.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DT_JUMP_SCALE_STOCK 100
#define DT_JUMP_SCALE_MIN   25
#define DT_JUMP_SCALE_MAX   400

/* False (and nothing changes) outside DT_JUMP_SCALE_MIN..MAX. */
bool dt_jump_set_scale(int percent);
int  dt_jump_scale(void);
/* Jumps the hook has rescaled since start. */
uint64_t dt_jump_rescaled(void);

#ifdef __cplusplus
}
#endif
