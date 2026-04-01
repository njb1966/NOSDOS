# NOS-DOS: NOS-SHELL
# Makefile - Build NOS-SHELL component.
# Requires: Open Watcom C (wcc, wlink, wmake)
# Usage: wmake -f Makefile   (from this directory)

CC     = wcc
LINK   = wlink
# -ms  = small memory model
# -bt=dos = 16-bit DOS target
# -q   = quiet (suppress banner)
# -w4  = warning level 4
# -e25 = stop after 25 errors
# -zq  = suppress copyright
# -ox  = optimise for speed (small model safe)
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/screen.obj $(OBJDIR)/input.obj $(OBJDIR)/panel.obj $(OBJDIR)/shell.obj

all : dirs $(BINDIR)/SHELL.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/screen.obj : screen.c screen.h
	$(CC) $(CFLAGS) screen.c -fo=$@

$(OBJDIR)/input.obj : input.c input.h screen.h
	$(CC) $(CFLAGS) input.c -fo=$@

$(OBJDIR)/panel.obj : panel.c panel.h screen.h
	$(CC) $(CFLAGS) panel.c -fo=$@

$(OBJDIR)/shell.obj : shell.c screen.h input.h panel.h
	$(CC) $(CFLAGS) shell.c -fo=$@

$(BINDIR)/SHELL.EXE : $(OBJS)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/SHELL.EXE del $(BINDIR)/SHELL.EXE
