MAKEFLAGS := -j

RELDIR := build-rel
DEBDIR := build-deb
SANDIR := build-san
MISCDIR := build

OBJDIR := /obj

SRCDIR := src

LIBDIRS := /usr/local/libc

LIBS := -lSDL3

CC := clang
CXX := clang++
CFLAGS := -MP -MMD -std=gnu23 -fwrapv -Wimplicit-fallthrough -Wall -Wextra -Werror=implicit-fallthrough -Isrc
CPPFLAGS := -MP -MMD -std=gnu++23 -fwrapv -Wimplicit-fallthrough -Wall -Wextra -Werror=implicit-fallthrough -Isrc

ifeq ($(FPS), 1) # monitor performance -- outputs frametime info via printf
	CFLAGS += -DFPSLOG
endif
ifeq ($(AUDMP), 1) # dump raw 2 channel 16 bit pcm audio to audioout.bin
	CFLAGS += -DDUMPAUDIO
endif
ifeq ($(THRD), 1) # use threads instead of coroutines -- UNSTABLE - NOT RECOMMENDED
	CFLAGS += -DREALTHREAD
endif
ifeq ($(GPUST), 1) # disable multithreaded ppus and gpu for testing purposes -- also disables per-pixel ppu & gpu emulation and vram timings
	CFLAGS += -DSINGLETHREADRASTER
endif
ifeq ($(GENPGO), 1) # generate pgo data
	CFLAGS += -fprofile-generate=prof
endif
ifeq ($(USEPGO), 1) # use converted pgo data
	CFLAGS += -fprofile-use=prof
endif
ifeq ($(NOLOG), 1) # disable misc logging prints, purposeful crash prints and FPS prints still occur
	CFLAGS += -DNOLOGGING
endif
ifeq ($(DIRBOOT), 1) # boot rom file directly, requires unencrypted rom
	CFLAGS += -DUSEDIRECTBOOT
endif


ifeq ($(DEB), 1) # debug build
	BUILDDIR := $(DEBDIR)
	CFLAGS += -march=x86-64-v3 -g -Og
	CPPFLAGS += -march=x86-64-v3 -g -Og
else
ifeq ($(SAN), 1) # debug w/ sanitizers
	BUILDDIR := $(SANDIR)
	CFLAGS += -march=x86-64-v3 -g -Og -fsanitize=undefined -fsanitize=address
	CPPFLAGS += -march=x86-64-v3 -g -Og -fsanitize=undefined -fsanitize=address
else
ifeq ($(REL), 1) # release build
	BUILDDIR := $(RELDIR)
	CFLAGS += -march=x86-64-v3 -O3 -flto=auto
	CPPFLAGS += -march=x86-64-v3 -O3 -flto=auto
else # standard build
	BUILDDIR := $(MISCDIR)
	CFLAGS += -mtune=native -O3 -flto=auto -g
# dont use lto for cppflags to keep the build times half-sane
	CPPFLAGS += -mtune=native -O3 -g
endif
endif
endif

OBJS := $(shell find $(SRCDIR) -name '*.c')
OBJS += libs/libco/libco.c
OBJS += $(shell find libs/imgui -maxdepth 1 -name '*.cpp')
OBJS := $(OBJS:%=$(BUILDDIR)$(OBJDIR)/%.o)

DEPS := $(OBJS:.o=.d)

$(BUILDDIR)$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)$(OBJDIR)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) $(CPPFLAGS) -c $< -o $@

$(BUILDDIR)/DualSOUP: $(OBJS)
	@echo linking...
	@$(CXX) $(CPPFLAGS) $(CFLAGS) $^ -o $@ -L$(LIBDIRS) $(LIBS)

.PHONY: clean
clean:
	@rm -rf build build-rel build-deb build-san

-include $(DEPS)