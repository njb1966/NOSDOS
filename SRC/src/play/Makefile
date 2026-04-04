# NOS-DOS: NOS-PLAY
# Makefile - Build NOSPLAY.EXE
# Requires: Open Watcom C (wcc, wlink, wmake)

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/profiles.obj $(OBJDIR)/nosplay.obj

all : dirs $(BINDIR)/NOSPLAY.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/profiles.obj : profiles.c profiles.h
	$(CC) $(CFLAGS) profiles.c -fo=$@

$(OBJDIR)/nosplay.obj : nosplay.c profiles.h
	$(CC) $(CFLAGS) nosplay.c -fo=$@

$(BINDIR)/NOSPLAY.EXE : $(OBJS)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/NOSPLAY.EXE del $(BINDIR)/NOSPLAY.EXE
