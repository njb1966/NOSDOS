# NOS-DOS: NOS-THROTTLE
# Makefile - Build THROTTLE.COM and TCTL.EXE
# Requires: NASM, Open Watcom C (wcc, wlink, wmake)

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

all : dirs $(BINDIR)/THROTTLE.COM $(BINDIR)/TCTL.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(BINDIR)/THROTTLE.COM : throttle.asm
	nasm -f bin throttle.asm -o $@

$(OBJDIR)/tctl.obj : tctl.c
	$(CC) $(CFLAGS) tctl.c -fo=$@

$(BINDIR)/TCTL.EXE : $(OBJDIR)/tctl.obj
	$(LINK) system dos name $@ file {$(OBJDIR)/tctl.obj}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/THROTTLE.COM del $(BINDIR)/THROTTLE.COM
	if exist $(BINDIR)/TCTL.EXE     del $(BINDIR)/TCTL.EXE
