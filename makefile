RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

LIBDIRS := /usr/local/libc

LIBS := -lSDL3

CC := clang
CFLAGS := -MP -MMD -std=gnu23 -fwrapv -Wimplicit-fallthrough -Wall -Wextra -Werror=implicit-fallthrough

ifeq ($(DEB), 1) # debug build
	BUILDDIR := $(DEBDIR)
	CFLAGS += -g -Og
	CFLAGS += -march=x86-64-v3
else
ifeq ($(SAN), 1) # debug w/ sanitizers
	BUILDDIR := $(SANDIR)
	CFLAGS += -g  -Og -fsanitize=undefined -fsanitize=address
	CFLAGS += -march=x86-64-v3
else
ifeq ($(REL), 1) # release build
	BUILDDIR := $(RELDIR)
	CFLAGS += -march=x86-64-v3 -O3 -flto
else # standard build
	BUILDDIR := $(MISCDIR)
	CFLAGS += -march=native -O3 -flto
endif
endif
endif

OBJS := $(shell find $(SRCDIR) -name '*.c')
OBJS += libs/libco/libco.c
OBJS := $(patsubst %,$(BUILDDIR)$(OBJDIR)/%,$(OBJS:.c=.o))

$(BUILDDIR)$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) -c -o $@ $< $(CFLAGS)

$(BUILDDIR)/DualSOUP: $(OBJS)
	@$(CC) -o $@ $^ $(CFLAGS) -L$(LIBDIRS) $(LIBS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san
