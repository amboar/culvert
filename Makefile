ifeq ($(shell basename "$$(which git)"),git)
ifeq ($(shell git rev-parse --is-inside-work-tree),true)
VERSION = $(shell git describe --tags --dirty --always)
endif
endif

ifeq ($(shell find version 2> /dev/null),version)
VERSION ?= $(shell cat version)
else
VERSION ?= Unknown
endif

CFLAGS = -O2 -flto -Werror -Wall -std=gnu99 -DVERSION='"$(VERSION)"'
CFLAGS += -DNDEBUG -I.
LDFLAGS = -flto
CC = gcc

SRCS += uart/suart.c uart/mux.c uart/vuart.c
SRCS += prompt.c ast.c ahb.c p2a.c shell.c pci.c l2a.c lpc.c sio.c ilpc.c doit.c
SRCS += clk.c wdt.c sfc.c flash.c mmio.c devmem.c log.c priv.c debug.c rev.c
SRCS += ts16.c tty.c

OBJS = $(SRCS:%.c=%.o)
DEPS = $(SRCS:%.c=%.d)

all: doit

ARCHIVE = $(shell basename "$$(pwd)")-$(VERSION)
dist: version
	git archive --format=tar --prefix=$(ARCHIVE)/ --output=$(ARCHIVE).tar master
	tar -uf $(ARCHIVE).tar --owner=0 --group=0 --transform='s|^|$(ARCHIVE)/|' $^
	gzip $(ARCHIVE).tar

doit: $(OBJS)

$(SRCS): version

version:
	git describe --tags --dirty > $@

cscope: $(SRCS)
	git ls-files | grep '\.[ch]$$' | xargs cscope -b

.PHONY: clean
clean:
	$(RM) version doit $(OBJS) $(DEPS)

%.o : %.c
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -c $< -o $@

% : %.o
	$(CROSS_COMPILE)$(CC) -o $@ $(LDFLAGS) $^ $(LOADLIBES)

-include $(DEPS)
