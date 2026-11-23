/*
 * native_mod.c - the cyc_core.h and cyc_mod.h calls hook callbacks make (what
 * they read and store), on the native runtime: the player's enhancements and the decompiled
 * routines (src/decomp/decomp_native.c).
 */
#include "native/native_runtime.h"

#include "cyc_core.h"
#include "cyc_mod.h"

#include <string.h>

const uint8_t *cyc_cpu_ram(void)
{
    return native_cpu_ram();
}

uint8_t cyc_mod_peek(uint16_t addr)
{
    uint8_t v = 0;
    native_peek(addr, &v);
    return v;
}

bool cyc_mod_peek_ok(uint16_t addr, uint8_t *value)
{
    return native_peek(addr, value);
}

bool cyc_mod_poke(uint16_t addr, uint8_t value)
{
    return native_poke(addr, value);
}

size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity)
{
    size_t size;
    const uint8_t *prg = native_prg(&size);
    if (out && prg && capacity >= size) {
        memcpy(out, prg, size);
    }
    return prg ? size : 0;
}
