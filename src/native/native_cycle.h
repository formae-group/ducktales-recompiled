/*
 * native_cycle.h - NvVideo (native/native_video.h) filled from NESRecomp's
 * cycle host through its public compositor API (cyc_render.h), at a frame's
 * end: what that frame was drawn with. Used by the per-frame check against
 * the PPU (native/native_check.c) and by a compositor that draws the
 * presented picture natively instead of capturing the PPU's background.
 *
 *   memories   CHR through the cartridge's mapping, CIRAM pages $2000 and
 *              $2400 (vertical mirroring), palette RAM, OAM
 *   segments   from the lines' records (cyc_frame_lines): a line whose
 *              $2000/$2001 bits and position continue the current segment
 *              extends it, any other starts one; no split columns (the
 *              records are taken at each line's first dot)
 */
#pragma once
#include <stdbool.h>

#include "native/native_video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* False when $2800/$2C00 do not mirror $2000/$2400 (not vertical mirroring). */
bool nv_cycle_fill(NvVideo *video);

#ifdef __cplusplus
}
#endif
