# NOS-DOS: NOS-BRIDGE
# Makefile - Build NOSLPT.COM, NOSCLIP.COM, NBRIDGE.EXE
# Requires: Open Watcom C (wcc, wlink, wmake), NASM

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

all : dirs $(BINDIR)/NOSLPT.COM $(BINDIR)/NOSCLIP.COM $(BINDIR)/NBRIDGE.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(BINDIR)/NOSLPT.COM : noslpt.asm
	nasm -f bin noslpt.asm -o $@

$(BINDIR)/NOSCLIP.COM : nosclip.asm
	nasm -f bin nosclip.asm -o $@

$(OBJDIR)/bridge.obj : bridge.c bridge.h
	$(CC) $(CFLAGS) bridge.c -fo=$@

$(BINDIR)/NBRIDGE.EXE : $(OBJDIR)/bridge.obj
	$(LINK) system dos name $@ file {$(OBJDIR)/bridge.obj}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/NOSLPT.COM  del $(BINDIR)/NOSLPT.COM
	if exist $(BINDIR)/NOSCLIP.COM del $(BINDIR)/NOSCLIP.COM
	if exist $(BINDIR)/NBRIDGE.EXE del $(BINDIR)/NBRIDGE.EXE
