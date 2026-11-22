#include "ducktales_jump.h"

#include "ducktales/events.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

static int s_percent = DT_JUMP_SCALE_STOCK;
static uint64_t s_rescaled;

/* A jump just copied the take-off velocity into Scrooge's: scale it and let
 * the game's own physics run the jump. A negative velocity is left alone. */
static EventResult scale_jump(EventId id, void *user)
{
    (void)id;
    (void)user;
    uint32_t v = Obj_GetVelocityY(PLAYER_SLOT);
    if (v & 0x8000) return EVENT_CONTINUE;
    v = v * (uint32_t)s_percent / 100;
    if (v > 0x7FFF) v = 0x7FFF;
    Obj_SetVelocityY(PLAYER_SLOT, (uint16_t)v);
    s_rescaled++;
    return EVENT_CONTINUE;
}

bool dt_jump_set_scale(int percent)
{
    if (percent < DT_JUMP_SCALE_MIN || percent > DT_JUMP_SCALE_MAX) return false;
    s_percent = percent;
    if (percent != DT_JUMP_SCALE_STOCK) Events_On(EVENT_JUMP_START, scale_jump, NULL);
    else Events_Off(EVENT_JUMP_START, scale_jump, NULL);
    return true;
}

int dt_jump_scale(void) { return s_percent; }

uint64_t dt_jump_rescaled(void) { return s_rescaled; }
