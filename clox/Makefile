# Homebrew's LLVM when it is installed, whatever is on PATH otherwise: an
# empty LLVM_BIN leaves the tool names bare. The preference is not cosmetic --
# Apple's clang has no working LeakSanitizer on macOS.
BREW_LLVM := /opt/homebrew/opt/llvm/bin
LLVM_BIN  := $(if $(wildcard $(BREW_LLVM)/clang),$(BREW_LLVM)/,)

# make always has a value for CC, so ?= here would never fire. Assigning only
# when the value is make's own built-in default leaves both `make CC=...` and
# a CC from the environment untouched.
ifeq ($(origin CC),default)
  CC := $(LLVM_BIN)clang
endif
CLANG_FORMAT := $(LLVM_BIN)clang-format
CLANG_TIDY   := $(LLVM_BIN)clang-tidy
LLVM_PROFDATA := $(LLVM_BIN)llvm-profdata
LLVM_COV     := $(LLVM_BIN)llvm-cov

# Every path below is anchored to this Makefile's own directory rather than to
# the working directory, so the build behaves the same under make -C and -f.
HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# compile_flags.txt is the one place the flags that decide what the code means
# are written -- language, warnings, sanitizers -- so clangd in the editor and
# this build cannot disagree. Appended here is only what a build needs and an
# editor does not: debug info, no optimization, warnings promoted to errors.
CFLAGS := $(shell cat $(HERE)compile_flags.txt) \
          -g3 -O0 -fno-omit-frame-pointer -Werror

# nothing beyond libc is linked in so far; both stay defined so the recipes
# and the flags stamp can name them
LDFLAGS :=
LDLIBS :=

# leak detection is only asked for when the compiler is the Homebrew one,
# since Apple's LeakSanitizer does not report on this platform
LEAK_OPT := $(if $(filter $(LLVM_BIN)clang,$(CC)),detect_leaks=1:,)

# The four LSAN_OPTIONS each switch off a class of root, so a block still
# reachable from a global, the stack, a register or thread-local storage is
# reported rather than excused. Allocated once and never freed is exactly the
# case worth seeing, and the default settings hide it.
RUN_ENV  := ASAN_OPTIONS=$(LEAK_OPT)detect_stack_use_after_return=1 \
            LSAN_OPTIONS=use_globals=0:use_stacks=0:use_registers=0:use_tls=0

APP := clox
SRC := $(HERE)src
TESTS := $(HERE)tests
VENDOR := $(HERE)vendor
BUILD := $(HERE)build

# Sources are globbed, so adding a file needs no edit here. Removing one does:
# it drops out of the link line, but the existing binary is still newer than
# every remaining prerequisite, so make leaves the deleted code in it until
# the next `make clean`.
SRCS := $(wildcard $(SRC)/*.c)
HDRS := $(wildcard $(SRC)/*.h)
OBJS := $(SRCS:$(SRC)/%.c=$(BUILD)/%.o)
EXEC := $(BUILD)/$(APP)

# tests/*.c are the suites, tests/support/*.c the shared fixtures and helpers
TEST_SRCS := $(wildcard $(TESTS)/*.c) $(wildcard $(TESTS)/support/*.c)
TEST_HDRS := $(wildcard $(TESTS)/support/*.h)
TEST_OBJS := $(TEST_SRCS:$(TESTS)/%.c=$(BUILD)/tests/%.o)
TEST_EXEC := $(BUILD)/$(APP)_tests

# The test runner defines its own main, so the app's entry point is the one
# object it does not link. Nothing else in src/ is excluded: a test binary
# that covers a module has to link every module that one depends on.
LIB_OBJS := $(filter-out $(BUILD)/repl.o,$(OBJS))

FORMAT_FILES := $(SRCS) $(HDRS) $(TEST_SRCS) $(TEST_HDRS)

# Coverage needs the suite built a second way, so it gets a build directory of
# its own and the objects never mix. PROFRAW and PROFDATA hang off $(BUILD),
# which the coverage target overrides, so they land in that directory too.
COVERAGE_BUILD := $(BUILD)/coverage
COVERAGE_FLAGS := -fprofile-instr-generate -fcoverage-mapping
PROFRAW := $(BUILD)/tests.profraw
PROFDATA := $(BUILD)/tests.profdata

# The release build is the same sources with the development scaffolding taken
# out: the sanitizers, the debug info and the pinned frame pointer come off,
# optimization goes on, and NDEBUG compiles the internal-invariant asserts
# away. Like coverage it gets a build directory of its own, so objects built
# two different ways never share one.
#
# -flto=thin makes every object a bitcode file and defers code generation to
# the link, which is where a call from one .c into another can finally be
# inlined. It belongs in CFLAGS rather than LDFLAGS because it has to reach
# both halves, and the link recipes already pass CFLAGS.
RELEASE_BUILD := $(BUILD)/release
RELEASE_CFLAGS := $(filter-out -fsanitize=% -g3 -O0 -fno-omit-frame-pointer,$(CFLAGS)) \
                  -O2 -flto=thin -DNDEBUG

# Tests include the headers of what they cover by bare name, and the test
# framework as <utest.h>. Both are test-only, so they stay out of CPPFLAGS.
TEST_CPPFLAGS := -I$(SRC) -I$(VENDOR)/utest

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

# written by -MMD -MP in the compile recipes: one .d per object, listing the
# headers that object was built from, so editing a header rebuilds its users
DEPS := $(OBJS:%=%.d) $(VENDOR_OBJS:%=%.d) $(TEST_OBJS:%=%.d)

BUILD_FLAGS := $(CC) $(CFLAGS) $(VENDOR_CFLAGS) $(CPPFLAGS) $(TEST_CPPFLAGS) \
               $(LDFLAGS) $(LDLIBS)
FLAGS_STAMP := $(BUILD)/.flags

# `force` deliberately has no rule anywhere: a phony target that never exists
# is always out of date, which is what keeps the flags stamp reconsidered on
# every run.
.PHONY: run check test release run-release coverage coverage-report doctor clean format format-check tidy force

$(EXEC): $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(EXEC)

# $(BUILD) sits after the | as an order-only prerequisite: the directory has to
# exist, but a directory's mtime changes on every write inside it, and as a
# normal prerequisite that would rebuild every object whenever any one of them
# was written.
$(BUILD)/%.o: $(SRC)/%.c $(FLAGS_STAMP) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -MF $@.d -c $< -o $@

$(BUILD)/vendor/%.o: $(VENDOR)/%.c $(FLAGS_STAMP) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CPPFLAGS) -MMD -MP -MF $@.d -c $< -o $@

# utest.h holds up under the project's own warning set, so tests build with
# CFLAGS unchanged and no vendored objects to link: only repl.c uses isocline.
$(TEST_EXEC): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(TEST_EXEC)

$(BUILD)/tests/%.o: $(TESTS)/%.c $(FLAGS_STAMP) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_CPPFLAGS) -MMD -MP -MF $@.d -c $< -o $@

# Every object depends on this stamp, so changing a compiler or a flag rebuilds
# the tree instead of mixing objects built two different ways. The cmp is what
# makes that affordable: the file is rewritten only when the flags actually
# differ, since writing it unconditionally would bump its mtime and rebuild
# everything on every run.
$(FLAGS_STAMP): force | $(BUILD)
	@echo '$(BUILD_FLAGS)' | cmp -s - $@ || echo '$(BUILD_FLAGS)' > $@

$(BUILD):
	mkdir -p $@

# the sanitizers are compiled into the binary; RUN_ENV only tunes what they
# do once it runs, which is why both run and test set it
run: $(EXEC)
	@$(RUN_ENV) $< $(ARGS)

# ARGS reaches the runner: --filter=table.* to narrow, --list-tests to see
# the names, --output=<file> for an xunit report.
test: $(TEST_EXEC)
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
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

# the gate's counterpart to format: reports and fails, rewrites nothing
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

tidy:
	$(CLANG_TIDY) $(SRCS) -- $(CFLAGS) $(CPPFLAGS)

# what has to hold for every change, in one command; anything slower than a
# keystroke -- coverage, a fuzzing run -- earns its own target instead
check: format-check tidy test

# Recursive for the same reason coverage is: BUILD and CFLAGS have to differ
# for the whole tree, flags stamp included, not for a single recipe. No goal is
# named, so the sub-make builds its default one, which is $(EXEC) under the
# overridden BUILD.
release:
	@$(MAKE) --no-print-directory BUILD=$(RELEASE_BUILD) \
	         CFLAGS='$(RELEASE_CFLAGS)'

# run's counterpart for the other build. No RUN_ENV: the sanitizers are not
# compiled into this binary, so there is nothing for those variables to tune.
run-release: release
	@$(RELEASE_BUILD)/$(APP) $(ARGS)

# A report rather than a gate, so it stays out of check and a failing test does
# not stop it: what it is good for is naming code that has never run at all.
coverage:
	@$(MAKE) --no-print-directory BUILD=$(COVERAGE_BUILD) \
	         CFLAGS='$(CFLAGS) $(COVERAGE_FLAGS)' coverage-report

# Only ever run by the recursive call above, where BUILD and CFLAGS carry the
# coverage settings. The report covers src/ alone: the tests and the vendored
# code are what does the covering.
coverage-report: $(TEST_EXEC)
	@LLVM_PROFILE_FILE=$(PROFRAW) $(RUN_ENV) $< $(ARGS) || true
	@$(LLVM_PROFDATA) merge -sparse $(PROFRAW) -o $(PROFDATA)
	@$(LLVM_COV) report $< -instr-profile=$(PROFDATA) \
	    -ignore-filename-regex='(vendor|tests)/'

# -include rather than include: on a clean tree no .d file exists yet, and the
# compile that needs one is the same compile that writes it. -MP puts a phony
# target in each .d for every header it lists, so deleting a header does not
# leave the build stuck on a prerequisite nothing can make.
-include $(DEPS)
