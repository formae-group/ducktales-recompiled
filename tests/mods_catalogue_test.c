/*
 * mods_catalogue_test.c - order, switches, rescan and restart bookkeeping
 * with loose fixture folders (no ROM or running machine).
 *
 *   mods_catalogue_test <empty scratch dir>
 */
#include "mods/core/fm_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char TARGET[] = "0000000000000000000000000000000000000000000000000000000000000000";
static int s_failures;

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    s_failures += !ok;
}

static void fixture(const char *root, const char *folder, const char *id, const char *name, const char *target)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", root, folder);
    if (!fm_make_dir(path)) {
        perror(path);
        exit(2);
    }
    snprintf(path, sizeof path, "%s/%s/mod.toml", root, folder);
    FILE *f = fm_open_file(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fprintf(f, "id = \"%s\"\nname = \"%s\"\nversion = \"1\"\ngame = \"test\"\ntarget = \"%s\"\n", id, name, target);
    if (fclose(f)) {
        perror(path);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: mods_catalogue_test SCRATCH_DIR\n");
        return 2;
    }
    const char *dir = argv[1];
    const FmGame game = { "test", "target", "PRG", ".mod", "[test]" };
    fm_configure(&game);
    fm_set_target(TARGET);
    fixture(dir, "a", "a", "A", TARGET);
    fixture(dir, "b", "b", "B", TARGET);
    fixture(dir, "invalid", "b", "0 Refused", "not a signature");
    fm_add_folder(dir, false);
    fm_set_order("b:off,a:on");
    fm_scan();
    FmMod *b = fm_get(0), *a = fm_get(1), *invalid = fm_get(2);
    if (fm_count() != 3) {
        return 2;
    }
    check(!strcmp(b->name, "B") && !b->enabled && !b->error[0] && invalid->error[0],
          "a refused duplicate does not take the saved place or switch of the valid row");
    fm_set_enabled(true);
    a->loaded = true;
    fm_mark_loaded_set();
    fm_set_script_error(a->id, a->path, "failed handler");
    check(!fm_restart_needed(), "the current set is already loaded");
    fm_set_enabled(false);
    check(fm_restart_needed(), "turning mods off requires a restart");
    fm_set_enabled(true);
    fm_set_mod_enabled(0, true);
    check(fm_restart_needed(), "turning an unloaded mod on requires a restart");
    fm_set_mod_enabled(0, false);
    fm_move(0, 1);
    check(!fm_restart_needed(), "moving only a disabled mod does not change what will load");
    fm_rescan();
    check(fm_get(0)->loaded && !strcmp(fm_get(0)->script_error, "failed handler") && !fm_get(1)->enabled &&
              fm_get(2)->error[0] && !fm_restart_needed(),
          "rescan keeps the active mod's script error, the order and the switches");
    char current[1024], moved[1024];
    snprintf(current, sizeof current, "%s/a", dir);
    snprintf(moved, sizeof moved, "%s/moved", dir);
    if (rename(current, moved)) {
        perror(current);
        return 2;
    }
    fm_rescan();
    check(!fm_get(0)->loaded && !fm_get(0)->script_error[0] && fm_restart_needed(),
          "rescan notices a different source with the same id and does not attach the old script error");
    if (rename(moved, current)) {
        perror(moved);
        return 2;
    }
    fm_rescan();
    check(fm_get(0)->loaded && !strcmp(fm_get(0)->script_error, "failed handler") && !fm_restart_needed(),
          "restoring the original source clears the restart prompt and restores its script status");
    return s_failures != 0;
}
