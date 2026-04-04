/* NOS-DOS: NOS-PLAY
 * profiles.c - Game profile loader.
 *
 * Parses a minimal subset of .npkg definitions: [LAUNCHER] and [GAME].
 * Uses its own lightweight parser rather than linking against install.c,
 * keeping NOSPLAY.EXE small and independent of the full NPKG stack.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* fopen, fgets, fclose */
#include <string.h>   /* strcpy, strncpy, strcmp, strlen, strchr */
#include <ctype.h>    /* isspace, tolower */
#include "profiles.h"

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static void str_trim_right(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' '))
        s[--n] = '\0';
}

/* Case-insensitive comparison. */
static int nos_stricmp2(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* -----------------------------------------------------------------------
 * nos_profile_load
 * ----------------------------------------------------------------------- */

int nos_profile_load(const char *id, nos_game_profile_t *out)
{
    char path[64];
    FILE *fp;
    char  line[128];
    char *eq;
    int   in_launcher;
    int   in_game;
    int   len;

    /* Build path to cached definition. */
    strcpy(path, NOS_PLAY_DEFS_DIR);
    strcat(path, "\\");
    {
        int i;
        int base = (int)strlen(path);
        for (i = 0; id[i] && base + i < 62; i++)
            path[base + i] = (char)toupper((unsigned char)id[i]);
        path[base + i] = '\0';
    }
    strcat(path, ".NPK");

    fp = fopen(path, "r");
    if (!fp) return NOS_PROF_ERR_NODEF;

    /* Zero output. */
    {
        char *p = (char *)out;
        int   n = (int)sizeof(*out);
        while (n--) *p++ = 0;
    }
    strncpy(out->id, id, NOS_PROF_ID_LEN - 1);

    in_launcher = 0;
    in_game     = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        str_trim_right(line);
        len = (int)strlen(line);
        if (len == 0 || line[0] == '#') continue;

        /* Section header? */
        if (line[0] == '[') {
            in_launcher = (nos_stricmp2(line, "[LAUNCHER]") == 0);
            in_game     = (nos_stricmp2(line, "[GAME]") == 0);
            /* Capture package name/version from [PACKAGE] section too. */
            continue;
        }

        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        str_trim_right(line);
        eq++;
        while (*eq == ' ') eq++;

        if (in_launcher) {
            if (nos_stricmp2(line, "Label") == 0)
                strncpy(out->name, eq, NOS_PROF_NAME_LEN - 1);
            else if (nos_stricmp2(line, "Exec") == 0)
                strncpy(out->exec, eq, NOS_PROF_EXEC_LEN - 1);
            else if (nos_stricmp2(line, "Dir") == 0)
                strncpy(out->dir, eq, NOS_PROF_DIR_LEN - 1);
        }

        if (in_game) {
            out->has_game = 1;
            if (nos_stricmp2(line, "CPUPreset") == 0)
                strncpy(out->cpu_preset, eq, NOS_PROF_PRESET_LEN - 1);
            else if (nos_stricmp2(line, "MemProfile") == 0)
                strncpy(out->mem_profile, eq, NOS_PROF_MEM_LEN - 1);
            else if (nos_stricmp2(line, "SoundEnv") == 0)
                strncpy(out->sound_env, eq, NOS_PROF_SND_LEN - 1);
            else if (nos_stricmp2(line, "Notes") == 0)
                strncpy(out->notes, eq, NOS_PROF_NOTES_LEN - 1);
        }
    }

    fclose(fp);
    return NOS_PROF_OK;
}

/* -----------------------------------------------------------------------
 * nos_profile_list_games
 * ----------------------------------------------------------------------- */

int nos_profile_list_games(nos_game_list_t *list)
{
    FILE *fp;
    char  line[128];
    char *fields[8];
    int   nf;
    int   i;
    char *p;
    int   len;

    list->count = 0;

    fp = fopen(NOS_PLAY_REG_PATH, "r");
    if (!fp) return NOS_PROF_ERR_NOREG;

    while (fgets(line, (int)sizeof(line), fp) && list->count < NOS_PLAY_MAX_GAMES) {
        len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
            line[--len] = '\0';

        /* Skip magic header and blank lines. */
        if (len == 0 || line[0] == '#') continue;

        /* Split on tabs: ID \t Version \t InstallDir \t Category \t Date */
        nf = 0;
        p  = line;
        fields[nf++] = p;
        for (i = 0; line[i] && nf < 8; i++) {
            if (line[i] == '\t') {
                line[i] = '\0';
                fields[nf++] = &line[i + 1];
            }
        }

        if (nf < 4) continue;
        /* fields[3] = category */
        if (nos_stricmp2(fields[3], "game") != 0) continue;

        strncpy(list->entries[list->count].id,      fields[0], NOS_PROF_ID_LEN - 1);
        strncpy(list->entries[list->count].version, fields[1], NOS_PROF_VER_LEN - 1);
        /* name filled in as id for now; nos_profile_load() will give real name */
        strncpy(list->entries[list->count].name, fields[0], NOS_PROF_NAME_LEN - 1);
        list->count++;
    }

    fclose(fp);
    return list->count;
}
