/* NOS-DOS: NOS-SHELL
 * addapp.c - Add an entry to the F9 application launcher.
 *
 * Usage:   ADDAPP Label Dir Exec
 * Example: ADDAPP Rogue C:\GAMES\ROGUE C:\GAMES\ROGUE\ROGUE.EXE
 *
 * Appends one line to C:\NOS\SHELL\LAUNCHER.CFG in the format:
 *   Label|Dir|Exec
 *
 * This replaces the ADDAPP.BAT wrapper which could not write literal
 * pipe characters — DOS COMMAND.COM interprets '|' in expanded variables
 * as pipe operators regardless of how the string was constructed.
 *
 * License: GPL-2.0
 */

#include <stdio.h>
#include <string.h>

#define LAUNCHER_CFG "C:\\NOS\\SHELL\\LAUNCHER.CFG"

static void usage(void)
{
    printf("Usage:   ADDAPP Label Dir Exec\r\n");
    printf("Example: ADDAPP Rogue C:\\GAMES\\ROGUE C:\\GAMES\\ROGUE\\ROGUE.EXE\r\n");
    printf("\r\n");
    printf("Entries appear immediately in the F9 launcher in NOS-SHELL.\r\n");
    printf("To remove an entry, edit C:\\NOS\\SHELL\\LAUNCHER.CFG directly.\r\n");
}

int main(int argc, char *argv[])
{
    FILE *fp;

    if (argc != 4) {
        usage();
        return 1;
    }

    fp = fopen(LAUNCHER_CFG, "a");
    if (!fp) {
        printf("ADDAPP: cannot open %s for writing\r\n", LAUNCHER_CFG);
        return 1;
    }

    fprintf(fp, "%s|%s|%s\r\n", argv[1], argv[2], argv[3]);
    fclose(fp);

    printf("Added to launcher: %s\r\n", argv[1]);
    return 0;
}
