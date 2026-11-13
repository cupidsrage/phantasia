# Phantasia server build
#
#   make                          - build ./phantasia
#   make PHANTASIA_DIR=/some/dir  - set where bin/, data/ and log/ live
#
CC            ?= gcc
PHANTASIA_DIR ?= /app/phantasia
CFLAGS        ?= -g -O2
override CFLAGS += -D_REENTRANT -D_GNU_SOURCE -w -fcommon -DPHANTASIA_DIR='"$(PHANTASIA_DIR)"'
override LDLIBS += -pthread -lm

OBJS = main.o account.o character.o commands.o event.o fight.o \
       file.o hack.o info.o init.o io.o itcombat.o md5c.o misc.o socket.o \
       stats.o tags.o treasure.o gameplay.o gear.o royal.o

phantasia : $(OBJS)
	$(CC) $(CFLAGS) -o phantasia $(OBJS) $(LDLIBS)

$(OBJS) : include.h global.h macros.h md5.h phantdefs.h phantstruct.h pathnames.h prototypes.h

clean :
	rm -f phantasia *.o

.PHONY : clean
