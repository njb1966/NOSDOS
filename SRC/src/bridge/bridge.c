/* NOS-DOS: NOS-BRIDGE
 * bridge.c - Host bridge management utility.
 *
 * Commands:
 *   NBRIDGE STATUS          show H:\ mount state, print queue, clipboard
 *   NBRIDGE MOUNT           attempt to mount H:\ on each VM platform
 *   NBRIDGE DIRS            create directory structure under H:\
 *   NBRIDGE PRINT <file>    copy <file> to H:\PRINT\ as the next PRINTnnn.PRN
 *   NBRIDGE CLIP GET        display contents of H:\CLIP\CLIP.TXT
 *   NBRIDGE CLIP PUT <text> write <text> to H:\CLIP\CLIP.TXT
 *   NBRIDGE CLIP CLEAR      delete H:\CLIP\CLIP.TXT
 *
 * Mount strategy (NBRIDGE MOUNT):
 *   1. Check if H:\ already accessible -- done.
 *   2. Try VirtualBox: NET USE H: \\VBOXSVR\NOSDOS
 *   3. Try VMware:     NET USE H: \\HGFS\NOSDOS
 *   4. Print manual setup instructions.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <dos.h>      /* int86, union REGS */
#include <stdio.h>    /* printf, fopen, fgets, fclose, fputc, fputs, rename, remove */
#include <string.h>   /* strcmp, strcpy, strcat, strlen, strncpy */
#include <stdlib.h>   /* system */
#include <ctype.h>    /* toupper */
#include <direct.h>   /* mkdir */
#include "bridge.h"

/* -----------------------------------------------------------------------
 * nos_bridge_h_mounted
 * ----------------------------------------------------------------------- */

int nos_bridge_h_mounted(void)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.al = 8;         /* drive 8 = H: (A=1, B=2, ..., H=8) */
    int86(0x21, &r, &r);
    return (r.x.ax != 0xFFFF) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * nos_bridge_ensure_dirs
 * ----------------------------------------------------------------------- */

int nos_bridge_ensure_dirs(void)
{
    int ok = 1;
    if (!nos_bridge_h_mounted()) {
        printf("NBRIDGE: H:\\ is not mounted.\r\n");
        return -1;
    }
    mkdir(BRIDGE_INBOX);
    mkdir(BRIDGE_OUTBOX);
    mkdir(BRIDGE_PRINT);
    mkdir(BRIDGE_CLIP);
    /* Verify by probing (mkdir returns -1 if dir exists -- that is fine). */
    printf("NBRIDGE: directory structure ready on H:\\\r\n");
    printf("  INBOX    OUTBOX    PRINT    CLIP\r\n");
    return ok;
}

/* -----------------------------------------------------------------------
 * next_print_path -- find next available H:\PRINT\PRINTnnn.PRN
 * ----------------------------------------------------------------------- */

static void next_print_path(char *out)
{
    int   i;
    FILE *f;
    char  path[32];

    for (i = 1; i <= BRIDGE_PRINT_MAX; i++) {
        sprintf(path, "H:\\PRINT\\PRINT%03d.PRN", i);
        f = fopen(path, "rb");
        if (!f) {
            strcpy(out, path);
            return;
        }
        fclose(f);
    }
    /* All slots full -- overwrite 001 */
    strcpy(out, "H:\\PRINT\\PRINT001.PRN");
}

/* -----------------------------------------------------------------------
 * cmd_status
 * ----------------------------------------------------------------------- */

static void cmd_status(void)
{
    union REGS r;
    unsigned long free_kb;
    int    mounted;
    FILE  *f;
    int    prn_count;
    int    i;
    char   path[32];

    mounted = nos_bridge_h_mounted();
    printf("NBRIDGE STATUS\r\n");
    printf("--------------\r\n");

    if (mounted) {
        r.h.ah = 0x36;
        r.h.al = 8;
        int86(0x21, &r, &r);
        free_kb = ((unsigned long)r.x.bx * r.x.ax * r.x.cx) >> 10;
        printf("H:\\       MOUNTED  (%lu KB free)\r\n", free_kb);
    } else {
        printf("H:\\       NOT MOUNTED\r\n");
        printf("  Run: NBRIDGE MOUNT\r\n");
        return;
    }

    /* Count files in H:\PRINT\ */
    prn_count = 0;
    for (i = 1; i <= BRIDGE_PRINT_MAX; i++) {
        sprintf(path, "H:\\PRINT\\PRINT%03d.PRN", i);
        f = fopen(path, "rb");
        if (f) { prn_count++; fclose(f); }
    }
    printf("H:\\PRINT  %d spool file(s)\r\n", prn_count);

    /* Clipboard */
    f = fopen(BRIDGE_CLIP_TXT, "r");
    if (f) {
        char line[80];
        int  c;
        long sz;
        fseek(f, 0L, SEEK_END);
        sz = ftell(f);
        rewind(f);
        fgets(line, (int)sizeof(line), f);
        fclose(f);
        /* strip newline */
        i = (int)strlen(line);
        while (i > 0 && (line[i-1] == '\n' || line[i-1] == '\r'))
            line[--i] = '\0';
        printf("H:\\CLIP   %ld bytes  \"%s\"\r\n", sz, line);
    } else {
        printf("H:\\CLIP   (empty)\r\n");
    }
}

/* -----------------------------------------------------------------------
 * cmd_mount
 * ----------------------------------------------------------------------- */

static void cmd_mount(void)
{
    if (nos_bridge_h_mounted()) {
        printf("NBRIDGE: H:\\ is already mounted.\r\n");
        nos_bridge_ensure_dirs();
        return;
    }

    printf("NBRIDGE: attempting to mount shared folder on H:\\\r\n");

    /* Attempt 1: VirtualBox */
    printf("  Trying VirtualBox (\\\\VBOXSVR\\NOSDOS)...\r\n");
    system("NET USE H: \\\\VBOXSVR\\NOSDOS 2>NUL");
    if (nos_bridge_h_mounted()) {
        printf("  Mounted via VirtualBox.\r\n");
        nos_bridge_ensure_dirs();
        return;
    }

    /* Attempt 2: VMware HGFS */
    printf("  Trying VMware (\\\\HGFS\\NOSDOS)...\r\n");
    system("NET USE H: \\\\HGFS\\NOSDOS 2>NUL");
    if (nos_bridge_h_mounted()) {
        printf("  Mounted via VMware.\r\n");
        nos_bridge_ensure_dirs();
        return;
    }

    /* Nothing worked -- print instructions */
    printf("\r\nNBRIDGE: automatic mount failed.  Manual setup:\r\n");
    printf("\r\n");
    printf("  VirtualBox:\r\n");
    printf("    1. VM Settings > Shared Folders\r\n");
    printf("    2. Add host folder, name it 'NOSDOS', auto-mount OFF\r\n");
    printf("    3. Install VirtualBox Guest Additions for DOS\r\n");
    printf("    4. Run: NET USE H: \\\\VBOXSVR\\NOSDOS\r\n");
    printf("\r\n");
    printf("  VMware:\r\n");
    printf("    1. VM Settings > Options > Shared Folders\r\n");
    printf("    2. Add host folder named 'NOSDOS'\r\n");
    printf("    3. Run: NET USE H: \\\\HGFS\\NOSDOS\r\n");
    printf("\r\n");
    printf("  QEMU:\r\n");
    printf("    Use -drive format=vvfat,file=/path/on/host to expose a\r\n");
    printf("    host directory as a FAT drive letter.\r\n");
    printf("    Example (qemu): -drive if=ide,index=1,format=vvfat,file=/tmp/share\r\n");
    printf("    Then run: NBRIDGE DIRS to create the directory structure.\r\n");
}

/* -----------------------------------------------------------------------
 * cmd_dirs
 * ----------------------------------------------------------------------- */

static void cmd_dirs(void)
{
    nos_bridge_ensure_dirs();
}

/* -----------------------------------------------------------------------
 * cmd_print
 * ----------------------------------------------------------------------- */

static void cmd_print(const char *src_path)
{
    char  dest[32];
    FILE *in;
    FILE *out;
    int   c;
    long  copied;

    if (!src_path || !*src_path) {
        printf("Usage: NBRIDGE PRINT <file>\r\n");
        return;
    }

    if (!nos_bridge_h_mounted()) {
        printf("NBRIDGE: H:\\ not mounted.  Run: NBRIDGE MOUNT\r\n");
        return;
    }

    in = fopen(src_path, "rb");
    if (!in) {
        printf("NBRIDGE: cannot open '%s'\r\n", src_path);
        return;
    }

    next_print_path(dest);
    out = fopen(dest, "wb");
    if (!out) {
        printf("NBRIDGE: cannot create '%s'\r\n", dest);
        fclose(in);
        return;
    }

    copied = 0;
    while ((c = fgetc(in)) != EOF) {
        fputc(c, out);
        copied++;
    }
    fclose(in);
    fclose(out);

    printf("NBRIDGE: %ld bytes spooled to %s\r\n", copied, dest);
}

/* -----------------------------------------------------------------------
 * cmd_clip
 * ----------------------------------------------------------------------- */

static void cmd_clip(int argc, char *argv[])
{
    /* argv[0]="CLIP", argv[1]="GET"/"PUT"/"CLEAR", argv[2]=text for PUT */
    const char *sub;
    FILE *f;
    char  buf[82];

    if (argc < 2) {
        printf("Usage: NBRIDGE CLIP GET|PUT <text>|CLEAR\r\n");
        return;
    }

    if (!nos_bridge_h_mounted()) {
        printf("NBRIDGE: H:\\ not mounted.  Run: NBRIDGE MOUNT\r\n");
        return;
    }

    sub = argv[1];

    if (strcmp(sub, "GET") == 0) {
        f = fopen(BRIDGE_CLIP_TXT, "r");
        if (!f) {
            printf("(clipboard empty)\r\n");
            return;
        }
        printf("Clipboard contents:\r\n");
        while (fgets(buf, (int)sizeof(buf), f))
            fputs(buf, stdout);
        printf("\r\n");
        fclose(f);
        return;
    }

    if (strcmp(sub, "PUT") == 0) {
        const char *text;
        int i;

        if (argc < 3) {
            printf("Usage: NBRIDGE CLIP PUT <text>\r\n");
            return;
        }

        f = fopen(BRIDGE_CLIP_TXT, "w");
        if (!f) {
            printf("NBRIDGE: cannot write to CLIP.TXT\r\n");
            return;
        }

        /* argv[2..] form the text; join with spaces */
        for (i = 2; i < argc; i++) {
            if (i > 2) fputc(' ', f);
            fputs(argv[i], f);
        }
        fputs("\r\n", f);
        fclose(f);
        printf("NBRIDGE: clipboard updated.\r\n");
        return;
    }

    if (strcmp(sub, "CLEAR") == 0) {
        remove(BRIDGE_CLIP_TXT);
        printf("NBRIDGE: clipboard cleared.\r\n");
        return;
    }

    printf("NBRIDGE CLIP: unknown subcommand '%s'\r\n", sub);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int  i;
    char cmd[16];

    if (argc < 2) {
        printf("NOS-BRIDGE v1.0 -- Host file exchange utility\r\n");
        printf("Usage: NBRIDGE <command> [args]\r\n");
        printf("  STATUS              show bridge state\r\n");
        printf("  MOUNT               mount H:\\ shared folder\r\n");
        printf("  DIRS                create H:\\ directory structure\r\n");
        printf("  PRINT <file>        spool file to H:\\PRINT\\\r\n");
        printf("  CLIP GET            show clipboard\r\n");
        printf("  CLIP PUT <text>     write text to clipboard\r\n");
        printf("  CLIP CLEAR          clear clipboard\r\n");
        return 0;
    }

    /* Uppercase the command. */
    strncpy(cmd, argv[1], (int)sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    for (i = 0; cmd[i]; i++)
        cmd[i] = (char)toupper((unsigned char)cmd[i]);

    /* Uppercase sub-commands too. */
    if (argc >= 3) {
        char *sub = argv[2];
        for (i = 0; sub[i]; i++)
            sub[i] = (char)toupper((unsigned char)sub[i]);
    }

    if (strcmp(cmd, "STATUS") == 0) { cmd_status();         return 0; }
    if (strcmp(cmd, "MOUNT")  == 0) { cmd_mount();          return 0; }
    if (strcmp(cmd, "DIRS")   == 0) { cmd_dirs();           return 0; }
    if (strcmp(cmd, "PRINT")  == 0) { cmd_print(argc >= 3 ? argv[2] : NULL); return 0; }
    if (strcmp(cmd, "CLIP")   == 0) { cmd_clip(argc - 1, argv + 1); return 0; }

    printf("NBRIDGE: unknown command '%s'\r\n", argv[1]);
    return 1;
}
