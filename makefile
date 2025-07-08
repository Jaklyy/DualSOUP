RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

CC := clang
CFLAGS := -MP -MMD -O3 -std=c23 -flto -fwrapv -Wimplicit-fallthrough -Wall -Wno-unused-variable -mavx2 -mlzcnt -mbmi2

ifeq ($(DEBUG), 1)
	BUILDDIR := $(DEBDIR)
	CFLAGS += -g
else
ifeq ($(SANITIZE), 1)
	BUILDDIR := $(SANDIR)
	CFLAGS += -g -fsanitize=undefined -fsanitize=address
else
ifeq ($(RELEASE), 1)
	BUILDDIR := $(RELDIR)
else #standard build
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

$(BUILDDIR)/CycleDS: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san
