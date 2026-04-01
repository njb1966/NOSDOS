/* NOS-DOS: NNET
 * nnet.c - Network tool command router.
 *
 * Usage:
 *   NNET STATUS              -- show network status
 *   NNET DHCP                -- run DHCP to obtain IP address
 *   NNET TIME                -- sync clock via NTP (SNTP)
 *   NNET PING <host>         -- ping a host
 *   NNET WEB <url>           -- fetch a web page (HTGET)
 *   NNET FTP <host>          -- FTP client
 *   NNET TELNET <host>       -- Telnet client
 *   NNET IRC <server>        -- IRC client (IRCjr)
 *   NNET LOOKUP <name>       -- DNS lookup (DNSTEST)
 *   NNET CONFIG              -- edit MTCP.CFG in EDIT.COM
 *
 * All mTCP tools are expected in C:\NOS\SYSTEM\.
 * mTCP reads its configuration from the file pointed to by %MTCPCFG%.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>   /* printf, sprintf */
#include <string.h>  /* strcmp, strcpy, strcat */
#include <stdlib.h>  /* system, exit */
#include "status.h"

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define SYS_PATH   "C:\\NOS\\SYSTEM\\"

/* -----------------------------------------------------------------------
 * Helper: spawn a tool with optional arguments
 * ----------------------------------------------------------------------- */

static void spawn_tool(const char *exe, const char *args)
{
    char cmd[128];
    strcpy(cmd, SYS_PATH);
    strcat(cmd, exe);
    if (args && *args) {
        strcat(cmd, " ");
        strcat(cmd, args);
    }
    system(cmd);
}

/* -----------------------------------------------------------------------
 * Usage
 * ----------------------------------------------------------------------- */

static void print_usage(void)
{
    printf("NOS-DOS Network Tool v0.1\r\n");
    printf("Usage: NNET <command> [args]\r\n\r\n");
    printf("  STATUS              Show network status\r\n");
    printf("  DHCP                Obtain IP via DHCP\r\n");
    printf("  TIME                Sync clock via NTP\r\n");
    printf("  PING <host>         Ping a host\r\n");
    printf("  WEB  <url>          Fetch a web page\r\n");
    printf("  FTP  <host>         FTP client\r\n");
    printf("  TELNET <host>       Telnet client\r\n");
    printf("  IRC  <server>       IRC client\r\n");
    printf("  LOOKUP <name>       DNS lookup\r\n");
    printf("  CONFIG              Edit MTCP.CFG\r\n");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char args[96];
    int  i;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* Build arg string from argv[2..] */
    args[0] = '\0';
    for (i = 2; i < argc; i++) {
        if (i > 2) strcat(args, " ");
        strcat(args, argv[i]);
    }

    /* Route by subcommand (case-insensitive via explicit checks) */
    if (strcmp(argv[1], "STATUS") == 0 || strcmp(argv[1], "status") == 0) {
        nos_status_show();
    } else if (strcmp(argv[1], "DHCP") == 0 || strcmp(argv[1], "dhcp") == 0) {
        spawn_tool("DHCP.EXE", "");
    } else if (strcmp(argv[1], "TIME") == 0 || strcmp(argv[1], "time") == 0) {
        spawn_tool("SNTP.EXE", "");
    } else if (strcmp(argv[1], "PING") == 0 || strcmp(argv[1], "ping") == 0) {
        if (argc < 3) { printf("Usage: NNET PING <host>\r\n"); return 1; }
        spawn_tool("PING.EXE", args);
    } else if (strcmp(argv[1], "WEB") == 0 || strcmp(argv[1], "web") == 0) {
        if (argc < 3) { printf("Usage: NNET WEB <url>\r\n"); return 1; }
        spawn_tool("HTGET.EXE", args);
    } else if (strcmp(argv[1], "FTP") == 0 || strcmp(argv[1], "ftp") == 0) {
        if (argc < 3) { printf("Usage: NNET FTP <host>\r\n"); return 1; }
        spawn_tool("FTP.EXE", args);
    } else if (strcmp(argv[1], "TELNET") == 0 || strcmp(argv[1], "telnet") == 0) {
        if (argc < 3) { printf("Usage: NNET TELNET <host>\r\n"); return 1; }
        spawn_tool("TELNET.EXE", args);
    } else if (strcmp(argv[1], "IRC") == 0 || strcmp(argv[1], "irc") == 0) {
        if (argc < 3) { printf("Usage: NNET IRC <server>\r\n"); return 1; }
        spawn_tool("IRCJR.EXE", args);
    } else if (strcmp(argv[1], "LOOKUP") == 0 || strcmp(argv[1], "lookup") == 0) {
        if (argc < 3) { printf("Usage: NNET LOOKUP <name>\r\n"); return 1; }
        spawn_tool("DNSTEST.EXE", args);
    } else if (strcmp(argv[1], "CONFIG") == 0 || strcmp(argv[1], "config") == 0) {
        system("EDIT.COM C:\\NOS\\SYSTEM\\MTCP.CFG");
    } else {
        printf("NNET: unknown command '%s'\r\n", argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}
