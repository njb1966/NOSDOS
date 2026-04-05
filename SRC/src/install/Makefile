# NOS-DOS: NOS-INSTALL
# Makefile - Build the TUI installer component.
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
# -ox  = optimise for speed
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/screen.obj $(OBJDIR)/install.obj

all : dirs $(BINDIR)/INSTALL.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/screen.obj : screen.c screen.h
	$(CC) $(CFLAGS) screen.c -fo=$@

$(OBJDIR)/install.obj : install.c screen.h boot16.h
	$(CC) $(CFLAGS) install.c -fo=$@

$(BINDIR)/INSTALL.EXE : $(OBJS)
	$(LINK) system dos name $@ OPTION STACK=8192 file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/INSTALL.EXE del $(BINDIR)/INSTALL.EXE
