/*
 * ducktales/tune.h - Scrooge's tunables: numbers the game reloads or stores
 * as immediates, which a mod can replace (docs/DECOMPILATION.md
 * "Tunables"). All are 8.8 fixed point, 256 being one pixel per frame:
 *
 *   TUNE_GRAVITY        $33 into $EF at the start of Player_Update ($E354);
 *                       site after the store ($E358). Low byte only
 *   TUNE_WALK_SPEED     $0180 into slot 0's speed at the normal walk ($E495);
 *                       site after the stores ($E4A3)
 *   TUNE_POGO_VELOCITY  $05A8 into slot 0's velocity at a pogo bounce
 *                       ($E6A3); site after the stores ($E6AD)
 *   TUNE_JUMP_VELOCITY  $04BB, the take-off velocity the land set-up stores
 *                       in $07A0:$0790; no site: Tune_FrameEnd rewrites it
 *
 * While a tunable is unset its site is disabled and nothing changes. A set
 * one replaces the game's value at its site, on the machine and in the
 * measuring call of replace mode, but not in check mode's isolated call,
 * where the original must agree with the C (which reads the game's value
 * outside replace mode: Tune_Value in src/decomp/player/player_internal.h).
 *
 * One value per tunable: a second player would need one per player.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TuneId {
    TUNE_GRAVITY,
    TUNE_WALK_SPEED,
    TUNE_POGO_VELOCITY,
    TUNE_JUMP_VELOCITY,
    TUNE_COUNT
} TuneId;

#define TUNE_UNSET (-1)

/* value 0-$FFFF (gravity 0-$FF), or TUNE_UNSET for the game's own. */
void Tune_Set(TuneId id, int32_t value);
/* The value set, or TUNE_UNSET. */
int32_t Tune_Get(TuneId id);
/* After each machine frame: the take-off velocity in $07A0:$0790 becomes the
 * tuned one where it holds the game's (or a value tuned before), and the
 * game's again once it is unset. */
void Tune_FrameEnd(void);

#ifdef __cplusplus
}
#endif
