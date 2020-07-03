ifeq ($(shell basename "$$(which git)"),git)
ifeq ($(shell git rev-parse --is-inside-work-tree 2> /dev/null),true)
VERSION = $(shell git describe --tags --dirty --always)
BRANCH = $(shell git rev-parse --abbrev-ref HEAD)
endif
endif

ifeq ($(shell find version 2> /dev/null),version)
VERSION ?= $(shell cat version)
else
VERSION ?= Unknown
endif

HOST := $(shell $(CROSS_COMPILE)$(CC) -dumpmachine | cut -d- -f1)

CFLAGS = -O2 -flto -Werror -Wall -std=gnu99 -DVERSION='"$(VERSION)"'
CFLAGS += -DNDEBUG -I.
ifneq (,$(wildcard arch/$(HOST)))
CFLAGS += -Iarch/$(HOST)
endif
LDFLAGS = -flto
CC = gcc

SRCS += uart/suart.c uart/mux.c uart/vuart.c
SRCS += prompt.c ast.c ahb.c p2a.c shell.c pci.c l2a.c sio.c ilpc.c doit.c
SRCS += clk.c wdt.c sfc.c flash.c mmio.c devmem.c log.c priv.c debug.c rev.c
SRCS += ts16.c tty.c

ifneq (,$(wildcard arch/$(HOST)/lpc.c))
CFLAGS += -DHAVE_LPC=1
SRCS += arch/$(HOST)/lpc.c
endif

OBJS = $(SRCS:%.c=%.o)
DEPS = $(SRCS:%.c=%.d)

all: doit

ARCHIVE = $(shell basename "$$(pwd)")-$(VERSION)
dist: version
	git archive --format=tar --prefix=$(ARCHIVE)/ --output=$(ARCHIVE).tar $(BRANCH)
	tar -uf $(ARCHIVE).tar --owner=0 --group=0 --transform='s|^|$(ARCHIVE)/|' $^
	gzip $(ARCHIVE).tar

doit: $(OBJS)

$(SRCS): version

.PHONY: version
version:
	echo $(VERSION) > $@

cscope: $(SRCS)
	git ls-files | grep '\.[ch]$$' | xargs cscope -b

.PHONY: clean
clean:
	$(RM) doit $(OBJS) $(DEPS)

%.o : %.c
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -c $< -o $@

% : %.o
	$(CROSS_COMPILE)$(CC) -o $@ $(LDFLAGS) $^ $(LOADLIBES)

-include $(DEPS)
