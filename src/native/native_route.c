/*
 * native_route.c - input routes for the native runtime (native_route.h).
 */
#include "native/native_route.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t parse_buttons(const char *s)
{
    static const struct {
        const char *name;
        uint8_t bit;
    } NAMES[] = {
        { "A", 0x80 },  { "B", 0x40 },    { "SELECT", 0x20 }, { "START", 0x10 },
        { "UP", 0x08 }, { "DOWN", 0x04 }, { "LEFT", 0x02 },   { "RIGHT", 0x01 },
    };

    uint8_t b = 0;
    while (*s) {
        while (*s == '+' || *s == ' ' || *s == '\t') {
            s++;
        }
        size_t n = strcspn(s, "+ \t\r\n");
        if (!n) {
            break;
        }
        for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
            if (strlen(NAMES[i].name) == n && !strncmp(s, NAMES[i].name, n)) {
                b |= NAMES[i].bit;
            }
        }
        s += n;
    }
    return b;
}

bool native_route_load(NativeRoute *r, const char *path)
{
    memset(r, 0, sizeof *r);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    size_t cap = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *p = line, *end;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#') {
            continue;
        }
        long frame = strtol(p, &end, 10);
        if (end == p) {
            continue;
        }
        p = end;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        uint8_t port = 0;
        if (p[0] >= '1' && p[0] <= '2' && p[1] == ':') {
            port = (uint8_t)(p[0] - '1'), p += 2;
        }
        if (r->count == cap) {
            cap = cap ? cap * 2 : 256;
            r->steps = (NativeRouteStep *)realloc(r->steps, cap * sizeof *r->steps);
        }
        r->steps[r->count++] = (NativeRouteStep){ frame, port, *p == '-' ? 0 : parse_buttons(p) };
    }
    fclose(f);
    return true;
}

void native_route_frame(NativeRoute *r, long frame, uint8_t held[2])
{
    while (r->next < r->count && r->steps[r->next].frame <= frame) {
        r->held[r->steps[r->next].port] = r->steps[r->next].buttons;
        r->next++;
    }
    held[0] = r->held[0];
    held[1] = r->held[1];
}
