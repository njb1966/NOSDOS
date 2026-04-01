# NOS-DOS: NOS-MEM
# Makefile - Build NOS-MEM component.
# Requires: Open Watcom C (wcc, wlink, wmake)
# Usage: wmake -f Makefile   (from this directory)

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/nosmem.obj

all : dirs $(BINDIR)/NOSMEM.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/nosmem.obj : nosmem.c
	$(CC) $(CFLAGS) nosmem.c -fo=$@

$(BINDIR)/NOSMEM.EXE : $(OBJS)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/NOSMEM.EXE del $(BINDIR)/NOSMEM.EXE
