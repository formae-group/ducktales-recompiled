/*
 * ducktales_host.h - what the systems layer (ducktales_host.c) shares with
 * the Formae UI and the program entry: the enhancements as config.ini keeps
 * them, apart from the live values (game/enhancements.h), which a
 * command-line option may override for one run.
 */
#pragma once
#include "game/enhancements.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int  ducktales_host_saved_value(DtEnh e);
void ducktales_host_set_saved_value(DtEnh e, int value);
/* config.ini [Game]: one key (as the host's load_setting gets it) and all of
 * ours ("Key = value" lines, as the host's save_settings writes them). */
void ducktales_host_load_game_setting(const char *key, const char *value);
void ducktales_host_write_game_settings(FILE *f);

#ifdef __cplusplus
}
#endif
