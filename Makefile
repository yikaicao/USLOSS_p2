
TARGET = libphase2.a
ASSIGNMENT = 452phase2
CC = gcc
AR = ar

COBJS = phase2.o
CSRCS = ${COBJS:.o=.c}

HDRS = message.h

# When using your phase1 library:
PHASELIB = phase1

# When using one of Patrick's phase1 libraries
#PHASE1LIB = patrickphase1debug
#PHASE1LIB = patrickphase1

INCLUDE = ./usloss/include

CFLAGS = -Wall -g -std=gnu99 -I${INCLUDE} -I.

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
        CFLAGS += -D_XOPEN_SOURCE
endif

LDFLAGS += -L./usloss/lib -L.

TESTDIR = testcases
TESTS= test00 test01 test02 test03 test04 test05 test06 test07 test08 \
       test09 test10 test11 test12 test13 test14 test15 test16 test17 \
       test18 test19 test20 test21 test22

LIBS = -l$(PHASE1LIB) -lphase2 -lusloss

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS) 

$(TESTS):	$(TARGET) $(TESTDIR)/$$@.c p1.o
	$(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
	$(CC) $(LDFLAGS) -o $@ $@.o $(LIBS) p1.o

clean:
	rm -f $(COBJS) $(TARGET) core term*.out test*.o $(TESTS) p1.o

phase2.o:	message.h

submit: $(CSRCS) $(HDRS) Makefile
	tar cvzf phase2.tgz $(CSRCS) $(HDRS) Makefile
