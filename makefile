RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

CC := clang
CFLAGS := -MP -MMD -O3 -flto -std=gnu23 -fwrapv -Wimplicit-fallthrough -Wall -Wextra -Werror=implicit-fallthrough

ifeq ($(DEB), 1) # debug build
	BUILDDIR := $(DEBDIR)
	CFLAGS += -g
	CFLAGS += -march=x86-64-v3
else
ifeq ($(SAN), 1) # debug w/ sanitizers
	BUILDDIR := $(SANDIR)
	CFLAGS += -g -fsanitize=undefined -fsanitize=address
	CFLAGS += -march=x86-64-v3
else
ifeq ($(REL), 1) # release build
	BUILDDIR := $(RELDIR)
	CFLAGS += -march=x86-64-v3
else # standard build
	BUILDDIR := $(MISCDIR)
	CFLAGS += -march=native
endif
endif
endif

OBJS := $(shell find $(SRCDIR) -name '*.c')
OBJS += libs/libco/libco.c
OBJS := $(patsubst %,$(BUILDDIR)$(OBJDIR)/%,$(OBJS:.c=.o))

$(BUILDDIR)$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILDDIR)/DualSOUP: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san
