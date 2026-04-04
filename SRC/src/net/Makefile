# NOS-DOS: NNET
# Makefile - Build the NNET network tool.
# Requires: Open Watcom C (wcc, wlink, wmake)

CC     = wcc
LINK   = wlink
CFLAGS = -ms -bt=dos -q -w4 -e25 -zq -ox

OBJDIR = obj
BINDIR = bin

OBJS = $(OBJDIR)/mtcpcfg.obj $(OBJDIR)/status.obj $(OBJDIR)/nnet.obj

all : dirs $(BINDIR)/NNET.EXE .SYMBOLIC

dirs : .SYMBOLIC
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)

$(OBJDIR)/mtcpcfg.obj : mtcpcfg.c mtcpcfg.h
	$(CC) $(CFLAGS) mtcpcfg.c -fo=$@

$(OBJDIR)/status.obj : status.c status.h mtcpcfg.h
	$(CC) $(CFLAGS) status.c -fo=$@

$(OBJDIR)/nnet.obj : nnet.c status.h
	$(CC) $(CFLAGS) nnet.c -fo=$@

$(BINDIR)/NNET.EXE : $(OBJS)
	$(LINK) system dos name $@ file {$(OBJS)}

clean : .SYMBOLIC
	if exist $(OBJDIR)/*.obj del $(OBJDIR)/*.obj
	if exist $(BINDIR)/NNET.EXE del $(BINDIR)/NNET.EXE
