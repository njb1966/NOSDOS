# NOS-DOS: NOS-DETECT
# Makefile - Build NOS-DETECT component.
# Requires: Open Watcom C (wcc, wlink, wmake)
# Usage: wmake -f Makefile   (from this directory)

CC     = wcc
LINK   = wlink
# -ms  small memory model (near code + near data, each <=64KB)
# -bt=dos  target 16-bit DOS
# -q   quiet (suppress banner)
# -w4  warning level 4
# -e25 stop after 25 errors
# -zq  suppress "file compiled" message
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/memory.obj $(OBJDIR)/video.obj $(OBJDIR)/mouse.obj $(OBJDIR)/sound.obj $(OBJDIR)/network.obj $(OBJDIR)/genconf.obj $(OBJDIR)/detect.obj

all : dirs $(BINDIR)/DETECT.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/memory.obj : memory.c memory.h
	$(CC) $(CFLAGS) memory.c -fo=$@

$(OBJDIR)/video.obj : video.c video.h
	$(CC) $(CFLAGS) video.c -fo=$@

$(OBJDIR)/mouse.obj : mouse.c mouse.h
	$(CC) $(CFLAGS) mouse.c -fo=$@

$(OBJDIR)/sound.obj : sound.c sound.h
	$(CC) $(CFLAGS) sound.c -fo=$@

$(OBJDIR)/network.obj : network.c network.h
	$(CC) $(CFLAGS) network.c -fo=$@

$(OBJDIR)/genconf.obj : genconf.c genconf.h memory.h video.h mouse.h sound.h network.h
	$(CC) $(CFLAGS) genconf.c -fo=$@

$(OBJDIR)/detect.obj : detect.c memory.h video.h mouse.h sound.h network.h genconf.h
	$(CC) $(CFLAGS) detect.c -fo=$@

$(BINDIR)/DETECT.EXE : $(OBJS)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/DETECT.EXE del $(BINDIR)/DETECT.EXE
