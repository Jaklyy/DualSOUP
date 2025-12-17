RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

LIBDIRS := /usr/local/libc

LIBS := -lSDL3

CC := clang
CFLAGS := -MP -MMD -std=gnu23 -fwrapv -Wimplicit-fallthrough -Wall -Wextra -Werror=implicit-fallthrough -Isrc

ifeq ($(FPS), 1) # monitor performance -- outputs frametime info via printf
	CFLAGS += -DMonitorFPS
endif

ifeq ($(THRD), 1) # use threads instead of coroutines -- NOT RECOMMENDED
	CFLAGS += -DUseThreads
endif

ifeq ($(PPUST), 1) # disable multithreaded ppus for testing purposes -- also disables per-pixel ppu emulation, and vram timings as those do not have a coroutine fallback
	CFLAGS += -DPPUST
endif

ifeq ($(GENPGO), 1) # generate pgo data
	CFLAGS += -fprofile-generate=prof
endif

ifeq ($(USEPGO), 1) # use converted pgo data
	CFLAGS += -fprofile-use=prof
endif

ifeq ($(DEB), 1) # debug build
	BUILDDIR := $(DEBDIR)
	CFLAGS += -g -Og
	CFLAGS += -march=x86-64-v3
else
ifeq ($(SAN), 1) # debug w/ sanitizers
	BUILDDIR := $(SANDIR)
	CFLAGS += -g -Og -fsanitize=undefined -fsanitize=address
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

DEPS := $(OBJS:.o=.d)

$(BUILDDIR)$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/DualSOUP: $(OBJS)
	@echo linking...
	@$(CC) $(CFLAGS) $^ -o $@ -L$(LIBDIRS) $(LIBS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san

-include $(DEPS)