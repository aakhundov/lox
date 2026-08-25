#include "library.h"

#include <stddef.h>
#include <time.h>
#include <unistd.h>

#include "object.h"
#include "value.h"

static clox_value_t clox_native_fn_clock(size_t arg_count, clox_value_t *args) {
  (void)arg_count;
  (void)args;
  return CLOX_NUMBER((double)clock() / CLOCKS_PER_SEC);
}

static clox_value_t clox_native_fn_sleep(size_t arg_count, clox_value_t *args) {
  (void)arg_count;
  sleep((unsigned int)CLOX_AS_NUMBER(args[0]));
  return CLOX_NIL;
}

const char *clox_library_fn_names[CLOX_LIBRARY_SIZE] = {
#define X(name) #name,
#include "natives.def"
#undef X
};

clox_native_fn_t *clox_library_fns[CLOX_LIBRARY_SIZE] = {
#define X(name) clox_native_fn_##name,
#include "natives.def"
#undef X
};
