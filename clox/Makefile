BREW_LLVM := /opt/homebrew/opt/llvm/bin
LLVM_BIN  := $(if $(wildcard $(BREW_LLVM)/clang),$(BREW_LLVM)/,)
ifeq ($(origin CC),default)
  CC := $(LLVM_BIN)clang
endif
CLANG_FORMAT := $(LLVM_BIN)clang-format
CLANG_TIDY   := $(LLVM_BIN)clang-tidy

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
CFLAGS := $(shell cat $(HERE)compile_flags.txt) \
          -g3 -O0 -fno-omit-frame-pointer -Werror

LDFLAGS :=
LDLIBS :=

LEAK_OPT := $(if $(filter $(LLVM_BIN)clang,$(CC)),detect_leaks=1:,)
RUN_ENV  := ASAN_OPTIONS=$(LEAK_OPT)detect_stack_use_after_return=1 \
            LSAN_OPTIONS=use_globals=0:use_stacks=0:use_registers=0:use_tls=0

APP := clox
SRC := $(HERE)src
VENDOR := $(HERE)vendor
BUILD := $(HERE)build

SRCS := $(wildcard $(SRC)/*.c)
HDRS := $(wildcard $(SRC)/*.h)
OBJS := $(SRCS:$(SRC)/%.c=$(BUILD)/%.o)
EXEC := $(BUILD)/$(APP)

# Third-party code, one entry per library. Sources are listed explicitly
# rather than globbed: isocline's src/isocline.c is a unity build that
# #includes its siblings, so globbing would compile them a second time.
VENDOR_SRCS := $(VENDOR)/isocline/src/isocline.c
VENDOR_OBJS := $(VENDOR_SRCS:$(VENDOR)/%.c=$(BUILD)/vendor/%.o)

# vendored headers are found by every translation unit, ours and theirs
override CPPFLAGS += -I$(VENDOR)/isocline/include

# Vendored code is not ours to keep warning-clean, so it builds outside the
# project's strict set: no -W..., no -pedantic-errors. Everything else is
# inherited, so sanitizers and debug info still cover the whole binary.
VENDOR_CFLAGS := $(filter-out -W% -pedantic-errors,$(CFLAGS))

DEPS := $(OBJS:%=%.d) $(VENDOR_OBJS:%=%.d)

BUILD_FLAGS := $(CC) $(CFLAGS) $(VENDOR_CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)
FLAGS_STAMP := $(BUILD)/.flags

.PHONY: run doctor clean format tidy force

$(EXEC): $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(EXEC)

$(BUILD)/%.o: $(SRC)/%.c $(FLAGS_STAMP) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -MF $@.d -c $< -o $@

$(BUILD)/vendor/%.o: $(VENDOR)/%.c $(FLAGS_STAMP) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CPPFLAGS) -MMD -MP -MF $@.d -c $< -o $@

$(FLAGS_STAMP): force | $(BUILD)
	@echo '$(BUILD_FLAGS)' | cmp -s - $@ || echo '$(BUILD_FLAGS)' > $@

$(BUILD):
	mkdir -p $@

run: $(EXEC)
	@$(RUN_ENV) $< $(ARGS)

doctor:
	@echo "CC     = $(CC)"
	@$(CC) --version | head -1
	@echo "target = `$(CC) -print-target-triple`"
	@echo "tidy   = $(CLANG_TIDY)"
	@echo "leaks  = $(if $(LEAK_OPT),on,off)"

clean:
	rm -rf $(BUILD)

format:
	$(CLANG_FORMAT) -i $(SRCS) $(HDRS)

tidy:
	$(CLANG_TIDY) $(SRCS) -- $(CFLAGS)

-include $(DEPS)
