/*
 * native_route.h - input routes (tests/routes) for the native runtime's
 * programs: the cycle host's --input format, one "<frame> BUTTONS" line per
 * change, buttons joined by +, "-" releases, "2:" selects controller 2; each
 * line holds from its frame on.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    long frame;
    uint8_t port, buttons;
} NativeRouteStep;

typedef struct {
    NativeRouteStep *steps;
    size_t count, next;
    uint8_t held[2];
} NativeRoute;

bool native_route_load(NativeRoute *r, const char *path);
/* The buttons both controllers hold on this frame (frames in increasing order). */
void native_route_frame(NativeRoute *r, long frame, uint8_t held[2]);

#ifdef __cplusplus
}
#endif
