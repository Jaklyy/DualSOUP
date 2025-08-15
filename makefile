RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

CC := clang
CFLAGS := -MP -MMD -O3 -std=gnu23 -flto -fwrapv -Wimplicit-fallthrough -Wall -Wno-unused-variable

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
OBJS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)$(OBJDIR)/%,$(OBJS:.c=.o))

$(BUILDDIR)$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILDDIR)/DualSOUP: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san
