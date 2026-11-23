/*
 * game_ram.c - RAM transactions for the typed game API (ducktales/ram.h).
 *
 * A transaction on the running machine copies the 2 KiB of CPU RAM into an
 * image, lets the code work on it through gRam, and stores every byte that
 * changed back with cyc_mod_poke, which goes through the compiled-code write
 * watch like a CPU store. A transaction that changes nothing writes nothing.
 */
#include "ducktales/ram.h"

#include "cyc_core.h"
#include "cyc_mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *gRam;

#define MAX_DEPTH 8

static struct {
    uint8_t *image;
    bool live;
} s_stack[MAX_DEPTH];

static int s_depth;
static uint8_t s_live_image[GAME_RAM_SIZE];

static void push(uint8_t *image, bool live)
{
    if (s_depth == MAX_DEPTH) {
        fprintf(stderr, "[ducktales] RAM transactions nested too deeply\n");
        abort();
    }
    s_stack[s_depth].image = image;
    s_stack[s_depth].live = live;
    s_depth++;
    gRam = image;
}

static void pop(void)
{
    s_depth--;
    gRam = s_depth ? s_stack[s_depth - 1].image : NULL;
}

void GameRam_Begin(void)
{
    if (s_depth) {
        push(gRam, false);
        return;
    }
    memcpy(s_live_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    push(s_live_image, true);
}

void GameRam_End(void)
{
    if (!s_depth) {
        return;
    }
    if (s_stack[s_depth - 1].live) {
        const uint8_t *live = cyc_cpu_ram();
        for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
            if (s_live_image[a] != live[a]) {
                cyc_mod_poke(a, s_live_image[a]);
            }
        }
    }
    pop();
}

void GameRam_Enter(uint8_t *image)
{
    push(image, false);
}

void GameRam_Leave(void)
{
    if (s_depth) {
        pop();
    }
}

bool GameRam_Open(void)
{
    return s_depth > 0;
}

uint8_t Game_ReadRom(uint16_t addr)
{
    return cyc_mod_peek(addr);
}
