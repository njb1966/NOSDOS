# NOS-DOS: NPKG
# Makefile - Build the NPKG package manager.
# Requires: Open Watcom C (wcc, wlink, wmake)
#
# Build individual objects by task phase:
#   Task 4.2: index.obj   (index parser / downloader)
#   Task 4.3: fetch.obj   (HTTP download engine)
#   Task 4.4: install.obj (archive extractor + post-install runner)
#   Task 4.5: registry.obj(installed package database)
#   Task 4.6: npkg.obj    (command router / main)

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/index.obj $(OBJDIR)/fetch.obj $(OBJDIR)/install.obj $(OBJDIR)/registry.obj $(OBJDIR)/npkg.obj

all : dirs $(BINDIR)/NPKG.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/index.obj : index.c index.h
	$(CC) $(CFLAGS) index.c -fo=$@

$(OBJDIR)/fetch.obj : fetch.c fetch.h
	$(CC) $(CFLAGS) fetch.c -fo=$@

$(OBJDIR)/install.obj : install.c install.h fetch.h index.h
	$(CC) $(CFLAGS) install.c -fo=$@

$(OBJDIR)/registry.obj : registry.c registry.h
	$(CC) $(CFLAGS) registry.c -fo=$@

$(OBJDIR)/npkg.obj : npkg.c index.h fetch.h install.h registry.h
	$(CC) $(CFLAGS) npkg.c -fo=$@

$(BINDIR)/NPKG.EXE : $(OBJS)
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/NPKG.EXE del $(BINDIR)/NPKG.EXE
