# Vendored dependencies

Third-party source copied into this tree. It is built by the top-level
`Makefile` but deliberately sits outside `src/`, so it is not swept into
`format`, `tidy`, or the strict warning set those targets imply.

## isocline

Rich line editor used by the clox REPL: multi-line editing, history with
incremental `ctrl-r` search, UTF-8, no dependencies.

| | |
|---|---|
| Upstream | <https://github.com/daanx/isocline> |
| Version | tag `v1.1.0` |
| Fetched | 2026-08-14 |
| License | MIT (`isocline/LICENSE`) |

### What was copied

`include/isocline.h`, `LICENSE`, `readme.md`, and the `src/` build set — 34
files, ~400 KB. The source list was not hand-picked: `src/isocline.c` is a
unity build that `#include`s its siblings, so the set is the transitive
closure of local `#include` directives starting from that file and the public
header. Only `src/isocline.c` is compiled; the rest are pulled in by it.

Upstream's `test/`, `example/`, `cmake/` and docs are not copied.

### Local modifications

The tree above is upstream `v1.1.0` with the patches in `vendor/patches/`
applied, in filename order:

| Patch | Why |
|---|---|
| `0001-add-is-complete-callback.patch` | Adds `ic_set_is_complete()`, letting the application decide when multi-line input is finished. Upstream accepts on enter unless the line ends with `\`, which is not a rule a REPL can use. |
| `0002-add-history-save.patch` | Adds `ic_history_save()`. The history file is only written when `ic_readline()` returns, so `ic_history_add()`, `ic_history_remove_last()` and `ic_history_clear()` cannot persist anything on their own. |
| `0003-configurable-short-history-entries.patch` | Adds `ic_enable_history_short_entries()`. Upstream silently drops any accepted input of one character or less, which makes a one-character program unrecallable and leaves an application unable to tell whether the entry it wants to remove was ever recorded. |

Each patch is a diff against the tree with the preceding ones applied, so they
apply in filename order.

Do not hand-edit anything under `isocline/`. A change that is genuinely needed
becomes a new numbered patch which is then applied, so the next upgrade is a
re-fetch plus a re-apply rather than an archaeology exercise.

Verified at import: applying the patches to a freshly fetched `v1.1.0` tree
reproduces `isocline/` byte for byte.

### Refreshing

Re-fetch the tree at a new tag, then re-apply the patches in
`vendor/patches/` in filename order. The fetch script used for the initial
import resolves the file set itself, so a new upstream layout does not need
the list here to be updated by hand.

## utest.h

Single-header unit test framework used by the `tests/` suite: tests register
themselves at load time, assertions come in fatal (`ASSERT_*`) and non-fatal
(`EXPECT_*`) forms and print both operands on failure, and the runner can emit
an xunit XML report.

| | |
|---|---|
| Upstream | <https://github.com/sheredom/utest.h> |
| Version | branch `master`, sha256 `469b743a41a66fac605d09a1608eb00cebdf451179505e5cbf22c882b40fb11f` |
| Fetched | 2026-08-20 |
| License | Unlicense (`utest/LICENSE`, also reproduced at the top of the header) |

### What was copied

`utest.h` and `LICENSE`, two files. Upstream publishes no release tags, so the
digest above is the pin: a re-fetch that does not reproduce it is a different
version. Upstream's `test/` and CI configuration are not copied.

Unlike isocline, this header compiles clean under the project's own strict
warning set, so tests are built with `CFLAGS` unchanged rather than with the
relaxed vendor flags.

### Local modifications

None.

### Refreshing

Re-fetch `utest.h` from the upstream default branch and record the new digest
here.
