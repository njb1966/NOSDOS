/* NOS-DOS: NOS-PLAY
 * profiles.h - Game profile types and loader interface.
 *
 * A game profile is the [GAME] section of an .npkg definition combined
 * with the [LAUNCHER] exec path.  NOS-PLAY reads this to know what CPU
 * preset, memory profile, and sound environment to apply before launch.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_PROFILES_H
#define NOS_PROFILES_H

/* -----------------------------------------------------------------------
 * Paths
 * ----------------------------------------------------------------------- */

/* Location of installed .npkg definition cache files. */
#define NOS_PLAY_DEFS_DIR    "C:\\NOS\\NPKG\\DEFS"

/* Installed-package registry (shared with NPKG). */
#define NOS_PLAY_REG_PATH    "C:\\NOS\\NPKG\\INSTALLED.DB"

/* -----------------------------------------------------------------------
 * nos_game_profile_t
 * ----------------------------------------------------------------------- */

#define NOS_PROF_ID_LEN       9
#define NOS_PROF_NAME_LEN     41
#define NOS_PROF_VER_LEN      13
#define NOS_PROF_EXEC_LEN     65
#define NOS_PROF_DIR_LEN      65
#define NOS_PROF_PRESET_LEN   11   /* SLOW477 + NUL */
#define NOS_PROF_MEM_LEN      5    /* GAME + NUL */
#define NOS_PROF_SND_LEN      81
#define NOS_PROF_NOTES_LEN    121

typedef struct {
    char id[NOS_PROF_ID_LEN];
    char name[NOS_PROF_NAME_LEN];
    char version[NOS_PROF_VER_LEN];
    char exec[NOS_PROF_EXEC_LEN];    /* from [LAUNCHER] Exec= */
    char dir[NOS_PROF_DIR_LEN];      /* from [LAUNCHER] Dir=  */
    char cpu_preset[NOS_PROF_PRESET_LEN]; /* from [GAME] CPUPreset= */
    char mem_profile[NOS_PROF_MEM_LEN];   /* from [GAME] MemProfile= */
    char sound_env[NOS_PROF_SND_LEN];     /* from [GAME] SoundEnv= */
    char notes[NOS_PROF_NOTES_LEN];       /* from [GAME] Notes= */
    char has_game;   /* 1 if [GAME] section was present */
} nos_game_profile_t;

/* -----------------------------------------------------------------------
 * nos_game_list_t -- compact list of installed games
 * ----------------------------------------------------------------------- */

#define NOS_PLAY_MAX_GAMES   32

typedef struct {
    char id[NOS_PROF_ID_LEN];
    char name[NOS_PROF_NAME_LEN];
    char version[NOS_PROF_VER_LEN];
} nos_game_entry_t;

typedef struct {
    nos_game_entry_t entries[NOS_PLAY_MAX_GAMES];
    int count;
} nos_game_list_t;

/* -----------------------------------------------------------------------
 * Return codes
 * ----------------------------------------------------------------------- */

#define NOS_PROF_OK          0
#define NOS_PROF_ERR_NODEF  -1   /* .npkg definition not found */
#define NOS_PROF_ERR_PARSE  -2   /* definition file is malformed */
#define NOS_PROF_ERR_NOREG  -3   /* INSTALLED.DB not found */
#define NOS_PROF_ERR_NOTFOUND -4 /* game ID not in registry */

/* -----------------------------------------------------------------------
 * Interface
 * ----------------------------------------------------------------------- */

/*
 * nos_profile_load
 *
 * Parses the cached .npkg definition for `id` (looked up in
 * NOS_PLAY_DEFS_DIR/<id>.NPK) and fills `out`.
 *
 * Returns NOS_PROF_OK or NOS_PROF_ERR_*.
 */
int nos_profile_load(const char *id, nos_game_profile_t *out);

/*
 * nos_profile_list_games
 *
 * Reads INSTALLED.DB and returns entries whose category is "game".
 * Fills `list` (caller provides; only first NOS_PLAY_MAX_GAMES entries
 * are returned).
 *
 * Returns number of games found, or NOS_PROF_ERR_NOREG on I/O failure.
 */
int nos_profile_list_games(nos_game_list_t *list);

#endif /* NOS_PROFILES_H */
