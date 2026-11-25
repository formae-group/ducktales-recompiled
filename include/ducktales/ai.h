/*
 * ducktales/ai.h - code of a mod's own for the objects of a kind (dt.ai,
 * docs/MODS.md "Object code").
 *
 * The object dispatch (bank 6 $8000) runs the code of each live slot 1-15 in
 * turn, chosen by its kind. A kind with a handler here gets two calls at each
 * of its objects' turns, inside a RAM transaction:
 *
 *   AI_BEFORE  before the kind's code; true skips that code for this turn
 *              (the dispatch jumps to kind 0's RTS instead), and so does an
 *              object the handler despawned
 *   AI_AFTER   once the kind's code returned, while the slot still holds a
 *              live object of that kind
 *
 * Original code reaches them at the dispatch's two sites (DT_SITE_AI_BEFORE
 * and DT_SITE_AI_AFTER), which are on only while a kind has a handler; the
 * decompiled dispatch calls them through Decomp_Ai.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_BEFORE,
    AI_AFTER
} AiPhase;

/* Returns true to skip the kind's code (AI_BEFORE only). */
typedef bool (*AiHandler)(uint8_t slot, AiPhase phase, void *user);

/* The handler of kind's objects; NULL leaves the kind to its own code. */
void Ai_Set(uint8_t kind, AiHandler fn, void *user);
/* Whether a kind has a handler. */
bool Ai_Active(void);
/* The call of phase at slot's turn, on the open RAM transaction. AI_BEFORE
 * returns whether the kind's code is skipped; then $00/$01, the dispatch's
 * jump vector, hold kind 0's handler. */
bool Ai_Run(uint8_t slot, AiPhase phase);

#ifdef __cplusplus
}
#endif
