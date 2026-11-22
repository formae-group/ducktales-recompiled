/*
 * native_present.h - the native runtime's picture for the window
 * (native/native_present.c): each machine frame's record (native_state.h)
 * drawn with the native renderer and offered through the cycle host's
 * presentation interface, so the port's presentation code runs unchanged.
 */
#pragma once
#include "native/native_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame that just ended: draw it and make it the current picture. */
void native_present_frame(const NativeFrame *frame);

#ifdef __cplusplus
}
#endif
