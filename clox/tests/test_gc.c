#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

// The collector is asked to run here, never waited for: clox_collect_garbage
// is the trigger, so a test builds exactly the heap it means to and collects at
// the one point it chooses. What a collection did is read back through the two
// things the allocator says for itself -- the running total and the list of
// objects it owns -- and, for the intern table, through ASan: a key left behind
// after its string was swept is a use-after-free on the next lookup of that
// text, which is the failure the weak-reference handling exists to prevent.

#define FILE_NAME "test.lox"
#define SOURCE ""
#define SOURCE_SIZE 512

// Enough live bytes that twice them clears the threshold floor, so the growth
// factor rather than the floor is what the next threshold comes from.
#define ABOVE_FLOOR_BYTES ((size_t)CLOX_GC_MIN_SIZE)

static const clox_pos_t POS = {.line = 1, .col = 1};

// A body for the natives these tests allocate. Nothing calls it; a native
// without a function is rejected before it is ever recorded.
static bool a_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                     clox_vm_t *vm) {
  (void)arg_count;
  (void)args;
  (void)vm;

  result->value = CLOX_NIL;
  return true;
}

// Whether the list runs in allocation order or against it is the allocator's
// own business, so the helpers below only ask how many objects are on it and
// whether a given one is among them.
static size_t count_objects(const clox_allocator_t *alloc) {
  size_t count = 0;
  for (const clox_object_t *obj = alloc->objects; obj != NULL; obj = obj->next) {
    count++;
  }
  return count;
}

static bool is_recorded(const clox_allocator_t *alloc, const void *object) {
  for (const clox_object_t *obj = alloc->objects; obj != NULL; obj = obj->next) {
    if (obj == object) {
      return true;
    }
  }
  return false;
}

// Parks bytes on the allocator until one more allocation of any size would
// cross the threshold, and answers how many it parked. The block is a plain
// array rather than an object, so a collection cannot take it and the heap the
// test built is what crosses the line.
static size_t fill_to_threshold(clox_allocator_t *alloc, char **block) {
  size_t room = alloc->next_gc_size - alloc->allocated_size - 1;
  *block = CLOX_ARRAY_ALLOCATE(alloc, char, room);
  memset(*block, 0, room);

  return room;
}

// ---------------------------------------------------------------------------
// the collector, over a heap a test builds by hand
// ---------------------------------------------------------------------------

struct gc {
  clox_allocator_t alloc;
};

UTEST_F_SETUP(gc) {
  CLOX_TEST_SKIP_UNDER_STRESS();

  clox_allocator_init(&utest_fixture->alloc);
}

UTEST_F_TEARDOWN(gc) {
  clox_allocator_free(&utest_fixture->alloc);
}

UTEST_F(gc, a_collection_over_an_empty_heap_takes_nothing) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)0, alloc->allocated_size);
  EXPECT_EQ(NULL, alloc->objects);
}

UTEST_F(gc, a_collection_reclaims_an_object_nothing_refers_to) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  (void)clox_string_copy(alloc, "unreferenced", 12);
  size_t before = alloc->allocated_size;
  ASSERT_EQ((size_t)1, count_objects(alloc));

  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)0, count_objects(alloc));
  EXPECT_TRUE(alloc->allocated_size < before);
}

UTEST_F(gc, a_collection_reclaims_an_object_of_every_kind) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_value_t slot = CLOX_NIL;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  function->upvalue_count = 1;
  clox_closure_t *closure = clox_new_closure(alloc, function);
  closure->upvalues[0] = clox_new_upvalue(alloc, &slot);
  (void)clox_new_native(alloc, "nt", 0, a_native);
  (void)clox_string_copy(alloc, "text", 4);

  ASSERT_EQ((size_t)5, count_objects(alloc));

  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)0, count_objects(alloc));
}

UTEST_F(gc, a_durable_object_survives_a_collection) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *kept = clox_string_copy(alloc, "kept", 4);
  clox_push_durable(alloc, (clox_object_t *)kept);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, kept));
  EXPECT_STREQ("kept", kept->chars);

  clox_pop_durable(alloc);
}

UTEST_F(gc, popping_a_durable_gives_the_next_collection_the_object) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *kept = clox_string_copy(alloc, "kept", 4);
  clox_push_durable(alloc, (clox_object_t *)kept);
  clox_collect_garbage(alloc);
  ASSERT_EQ((size_t)1, count_objects(alloc));

  clox_pop_durable(alloc);
  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)0, count_objects(alloc));
}

UTEST_F(gc, durables_nest) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *outer = clox_string_copy(alloc, "outer", 5);
  clox_push_durable(alloc, (clox_object_t *)outer);
  const clox_string_t *inner = clox_string_copy(alloc, "inner", 5);
  clox_push_durable(alloc, (clox_object_t *)inner);

  clox_collect_garbage(alloc);
  EXPECT_EQ((size_t)2, count_objects(alloc));

  // the inner push is released first; the outer one still holds
  clox_pop_durable(alloc);
  clox_collect_garbage(alloc);
  EXPECT_EQ((size_t)1, count_objects(alloc));
  EXPECT_TRUE(is_recorded(alloc, outer));

  clox_pop_durable(alloc);
}

// what a collection reaches through

UTEST_F(gc, a_collection_follows_a_closure_to_its_function) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  clox_closure_t *closure = clox_new_closure(alloc, function);
  // only the closure is held: the function has to be reached through it
  clox_push_durable(alloc, (clox_object_t *)closure);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, closure));
  EXPECT_TRUE(is_recorded(alloc, function));

  clox_pop_durable(alloc);
}

UTEST_F(gc, a_collection_follows_a_function_to_the_constants_in_its_chunk) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  const clox_string_t *constant = clox_string_copy(alloc, "constant", 8);
  ASSERT_TRUE(clox_write_constant(&function->chunk, OP_CONSTANT, CLOX_OBJECT(constant), POS));

  clox_push_durable(alloc, (clox_object_t *)function);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, function));
  EXPECT_TRUE(is_recorded(alloc, constant));
  EXPECT_STREQ("constant", constant->chars);

  clox_pop_durable(alloc);
}

// The deep case: a function reachable only as another function's constant,
// which is how a nested function is held once compilation has ended.
UTEST_F(gc, a_collection_follows_a_constant_that_is_itself_a_function) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_function_t *outer = clox_new_function(alloc, "outer", 5, 0, FILE_NAME, SOURCE);
  clox_function_t *inner = clox_new_function(alloc, "inner", 5, 0, FILE_NAME, SOURCE);
  const clox_string_t *deep = clox_string_copy(alloc, "deep", 4);
  ASSERT_TRUE(clox_write_constant(&inner->chunk, OP_CONSTANT, CLOX_OBJECT(deep), POS));
  ASSERT_TRUE(clox_write_constant(&outer->chunk, OP_CLOSURE, CLOX_OBJECT(inner), POS));

  clox_push_durable(alloc, (clox_object_t *)outer);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, inner));
  EXPECT_TRUE(is_recorded(alloc, deep));
  EXPECT_STREQ("deep", deep->chars);

  clox_pop_durable(alloc);
}

UTEST_F(gc, a_collection_follows_a_closure_to_its_upvalues) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_value_t slot = CLOX_NIL;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  function->upvalue_count = 2;
  clox_closure_t *closure = clox_new_closure(alloc, function);
  closure->upvalues[0] = clox_new_upvalue(alloc, &slot);
  closure->upvalues[1] = clox_new_upvalue(alloc, &slot);

  clox_push_durable(alloc, (clox_object_t *)closure);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, closure->upvalues[0]));
  EXPECT_TRUE(is_recorded(alloc, closure->upvalues[1]));

  clox_pop_durable(alloc);
}

// A closure whose upvalue slots are still empty is walked all the same: the
// array is allocated and NULL-filled before any capture reaches it.
UTEST_F(gc, a_collection_walks_a_closure_whose_upvalues_are_not_captured_yet) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  function->upvalue_count = 3;
  clox_closure_t *closure = clox_new_closure(alloc, function);

  clox_push_durable(alloc, (clox_object_t *)closure);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, closure));

  clox_pop_durable(alloc);
}

UTEST_F(gc, a_collection_follows_an_upvalue_to_the_value_it_closed_over) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_value_t slot = CLOX_NIL;

  clox_upvalue_t *upvalue = clox_new_upvalue(alloc, &slot);
  const clox_string_t *closed = clox_string_copy(alloc, "closed", 6);
  // closing an upvalue moves the value into the object and points at it there
  upvalue->closed = CLOX_OBJECT(closed);
  upvalue->location = &upvalue->closed;

  clox_push_durable(alloc, (clox_object_t *)upvalue);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, closed));
  EXPECT_STREQ("closed", closed->chars);

  clox_pop_durable(alloc);
}

// the intern table holds its keys weakly

UTEST_F(gc, a_kept_string_stays_interned) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *kept = clox_string_copy(alloc, "kept", 4);
  clox_push_durable(alloc, (clox_object_t *)kept);

  clox_collect_garbage(alloc);

  // interning is by identity, so the same text has to yield the same object
  EXPECT_EQ(kept, clox_string_copy(alloc, "kept", 4));

  clox_pop_durable(alloc);
}

// ASan is the oracle for the half that matters: had the swept string been left
// in the intern table, the lookup below would read a freed key's length, hash
// and characters. The count is what says the string was allocated afresh.
UTEST_F(gc, a_collected_string_leaves_the_intern_table) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  (void)clox_string_copy(alloc, "doomed", 6);
  ASSERT_EQ((size_t)1, count_objects(alloc));

  clox_collect_garbage(alloc);
  ASSERT_EQ((size_t)0, count_objects(alloc));

  const clox_string_t *reborn = clox_string_copy(alloc, "doomed", 6);

  EXPECT_EQ((size_t)1, count_objects(alloc));
  EXPECT_STREQ("doomed", reborn->chars);
  EXPECT_EQ((size_t)6, reborn->length);
}

// A tombstoned key must not cut a probe short: a string interned after a
// collection has to be findable past whatever the collection left behind.
UTEST_F(gc, strings_interned_after_a_collection_are_all_findable) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  for (size_t i = 0; i < 64; i++) {
    (void)clox_test_intern_indexed(alloc, i);
  }
  clox_collect_garbage(alloc);
  ASSERT_EQ((size_t)0, count_objects(alloc));

  const clox_string_t *first = clox_test_intern_indexed(alloc, 0);
  for (size_t i = 0; i < 64; i++) {
    const clox_string_t *again = clox_test_intern_indexed(alloc, i);
    ASSERT_EQ(again, clox_test_intern_indexed(alloc, i));
  }

  EXPECT_EQ(first, clox_test_intern_indexed(alloc, 0));
}

// when a collection happens on its own

UTEST_F(gc, nothing_is_collected_before_the_threshold_is_reached) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  (void)clox_string_copy(alloc, "unreferenced", 12);
  size_t objects = count_objects(alloc);

  // well short of the first threshold, so none of this collects anything
  for (size_t i = 0; i < 16; i++) {
    (void)clox_test_intern_indexed(alloc, i);
    objects++;
  }

  EXPECT_EQ(objects, count_objects(alloc));
  EXPECT_EQ((size_t)CLOX_GC_FIRST_SIZE, alloc->next_gc_size);
}

UTEST_F(gc, crossing_the_threshold_collects_without_being_asked) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  (void)clox_string_copy(alloc, "unreferenced", 12);
  ASSERT_EQ((size_t)1, count_objects(alloc));

  char *block = NULL;
  size_t bytes = fill_to_threshold(alloc, &block);
  // the fill stops one byte short, so nothing has collected yet
  ASSERT_EQ((size_t)1, count_objects(alloc));

  // any allocation at all is now the one that crosses
  const clox_string_t *trigger = clox_string_copy(alloc, "trigger", 7);
  clox_push_durable(alloc, (clox_object_t *)trigger);

  EXPECT_EQ((size_t)1, count_objects(alloc));
  EXPECT_TRUE(is_recorded(alloc, trigger));

  clox_pop_durable(alloc);
  CLOX_ARRAY_FREE(alloc, char, block, bytes);
}

UTEST_F(gc, the_next_threshold_is_what_survived_times_the_growth_factor) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  char *block = CLOX_ARRAY_ALLOCATE(alloc, char, ABOVE_FLOOR_BYTES);
  memset(block, 0, ABOVE_FLOOR_BYTES);

  clox_collect_garbage(alloc);

  // the block is not an object, so all of it survives the collection
  EXPECT_EQ(alloc->allocated_size * CLOX_GC_HEAP_GROW_FACTOR, alloc->next_gc_size);
  EXPECT_TRUE(alloc->next_gc_size > CLOX_GC_MIN_SIZE);

  CLOX_ARRAY_FREE(alloc, char, block, ABOVE_FLOOR_BYTES);
}

UTEST_F(gc, the_next_threshold_never_falls_below_the_minimum) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  (void)clox_string_copy(alloc, "unreferenced", 12);
  clox_collect_garbage(alloc);

  // No object survived. What the total still counts is the intern table's own
  // storage, which a collection tombstones keys in without giving back, and
  // that is far too little to reach the minimum on its own: without the floor
  // the next threshold would be a few hundred bytes and every allocation from
  // here on would collect.
  ASSERT_EQ((size_t)0, count_objects(alloc));
  ASSERT_TRUE(alloc->allocated_size * CLOX_GC_HEAP_GROW_FACTOR < CLOX_GC_MIN_SIZE);

  EXPECT_EQ((size_t)CLOX_GC_MIN_SIZE, alloc->next_gc_size);
}

UTEST_F(gc, collecting_twice_over_leaves_the_same_heap) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *kept = clox_string_copy(alloc, "kept", 4);
  clox_push_durable(alloc, (clox_object_t *)kept);
  (void)clox_string_copy(alloc, "unreferenced", 12);

  clox_collect_garbage(alloc);
  size_t objects = count_objects(alloc);
  size_t allocated = alloc->allocated_size;

  clox_collect_garbage(alloc);

  EXPECT_EQ(objects, count_objects(alloc));
  EXPECT_EQ(allocated, alloc->allocated_size);
  EXPECT_TRUE(is_recorded(alloc, kept));

  clox_pop_durable(alloc);
}

// the mark callbacks a collection asks for its roots

struct counting_ctx {
  size_t calls;
  clox_object_t *to_mark;
  clox_table_t *to_mark_table;
};

static void counting_callback(clox_allocator_t *alloc, void *ctx) {
  struct counting_ctx *counting = ctx;

  counting->calls++;
  if (counting->to_mark != NULL) {
    clox_mark_object(alloc, counting->to_mark);
  }
  if (counting->to_mark_table != NULL) {
    clox_table_mark_entries(counting->to_mark_table);
  }
}

UTEST_F(gc, a_registered_callback_runs_on_every_collection) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  struct counting_ctx counting = {0};

  void *handle = clox_register_mark_callback(alloc, counting_callback, &counting);
  ASSERT_TRUE(handle != NULL);

  clox_collect_garbage(alloc);
  EXPECT_EQ((size_t)1, counting.calls);
  clox_collect_garbage(alloc);
  EXPECT_EQ((size_t)2, counting.calls);

  EXPECT_TRUE(clox_unregister_mark_callback(alloc, handle));
}

UTEST_F(gc, an_unregistered_callback_stops_running) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  struct counting_ctx counting = {0};

  void *handle = clox_register_mark_callback(alloc, counting_callback, &counting);
  clox_collect_garbage(alloc);
  ASSERT_EQ((size_t)1, counting.calls);

  ASSERT_TRUE(clox_unregister_mark_callback(alloc, handle));
  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)1, counting.calls);
}

UTEST_F(gc, unregistering_reports_whether_the_handle_was_found) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  struct counting_ctx counting = {0};

  void *handle = clox_register_mark_callback(alloc, counting_callback, &counting);

  EXPECT_TRUE(clox_unregister_mark_callback(alloc, handle));
  // the same handle a second time is no longer registered
  EXPECT_FALSE(clox_unregister_mark_callback(alloc, handle));
  // and neither is something that never was
  EXPECT_FALSE(clox_unregister_mark_callback(alloc, &counting));
}

UTEST_F(gc, every_registered_callback_runs) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  struct counting_ctx first = {0};
  struct counting_ctx second = {0};
  struct counting_ctx third = {0};

  void *first_handle = clox_register_mark_callback(alloc, counting_callback, &first);
  void *second_handle = clox_register_mark_callback(alloc, counting_callback, &second);
  void *third_handle = clox_register_mark_callback(alloc, counting_callback, &third);

  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)1, first.calls);
  EXPECT_EQ((size_t)1, second.calls);
  EXPECT_EQ((size_t)1, third.calls);

  // removing the middle one leaves the other two reachable
  ASSERT_TRUE(clox_unregister_mark_callback(alloc, second_handle));
  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)2, first.calls);
  EXPECT_EQ((size_t)1, second.calls);
  EXPECT_EQ((size_t)2, third.calls);

  EXPECT_TRUE(clox_unregister_mark_callback(alloc, first_handle));
  EXPECT_TRUE(clox_unregister_mark_callback(alloc, third_handle));
}

UTEST_F(gc, an_object_a_callback_marks_survives) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, FILE_NAME, SOURCE);
  const clox_string_t *constant = clox_string_copy(alloc, "constant", 8);
  ASSERT_TRUE(clox_write_constant(&function->chunk, OP_CONSTANT, CLOX_OBJECT(constant), POS));

  struct counting_ctx counting = {.to_mark = (clox_object_t *)function};
  void *handle = clox_register_mark_callback(alloc, counting_callback, &counting);

  clox_collect_garbage(alloc);

  // nothing else holds either object: the callback is the only root
  EXPECT_TRUE(is_recorded(alloc, function));
  EXPECT_TRUE(is_recorded(alloc, constant));

  ASSERT_TRUE(clox_unregister_mark_callback(alloc, handle));
  clox_collect_garbage(alloc);
  EXPECT_EQ((size_t)0, count_objects(alloc));
}

// A table marked from a callback is how the VM's globals reach the collector,
// and it is the only way either table helper can be exercised without leaving
// the allocator part-way through a collection.
UTEST_F(gc, a_marked_table_keeps_its_keys_and_its_values) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_table_t table;
  clox_table_init(&table, alloc);

  const clox_string_t *key = clox_test_intern(alloc, "key");
  const clox_string_t *value = clox_string_copy(alloc, "value", 5);
  ASSERT_TRUE(clox_table_set(&table, key, CLOX_OBJECT(value)));

  struct counting_ctx counting = {.to_mark_table = &table};
  void *handle = clox_register_mark_callback(alloc, counting_callback, &counting);

  clox_collect_garbage(alloc);

  EXPECT_TRUE(is_recorded(alloc, key));
  EXPECT_TRUE(is_recorded(alloc, value));
  EXPECT_STREQ("key", key->chars);
  EXPECT_STREQ("value", value->chars);

  // the entry is still readable through the table it was marked from
  clox_value_t read = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&table, key, &read));
  EXPECT_VALUE_EQ(CLOX_OBJECT(value), read);

  ASSERT_TRUE(clox_unregister_mark_callback(alloc, handle));
  clox_table_free(&table);
}

UTEST_F(gc, an_unmarked_table_keeps_nothing) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_table_t table;
  clox_table_init(&table, alloc);

  const clox_string_t *key = clox_test_intern(alloc, "key");
  ASSERT_TRUE(clox_table_set(&table, key, CLOX_NUMBER(1.0)));
  ASSERT_EQ((size_t)1, count_objects(alloc));

  // no callback marks this table, so its key is unreachable
  clox_collect_garbage(alloc);

  EXPECT_EQ((size_t)0, count_objects(alloc));

  clox_table_free(&table);
}

// ---------------------------------------------------------------------------
// the collector, over a heap a running program built
// ---------------------------------------------------------------------------

// How many times the native below ran. utest runs one test at a time in one
// process, so a file-static counter is never shared between two runs.
static size_t forced_collections;

// The allocator to collect is the one the calling VM was built with, which the
// VM hands over with the call.
static bool collect_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                           clox_vm_t *vm) {
  (void)arg_count;
  (void)args;

  forced_collections++;
  clox_collect_garbage(vm->allocator);

  result->value = CLOX_NIL;
  return true;
}

struct gc_run {
  clox_allocator_t alloc;
  clox_compiler_t compiler;
  clox_vm_t vm;
  clox_test_printed_t printed;
  clox_test_errors_t errors;
  char source[SOURCE_SIZE];
};

UTEST_F_SETUP(gc_run) {
  CLOX_TEST_SKIP_UNDER_STRESS();

  clox_allocator_init(&utest_fixture->alloc);
  clox_compiler_init(&utest_fixture->compiler, &utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
  utest_fixture->printed = (clox_test_printed_t){0};
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_vm_set_print_fn(&utest_fixture->vm, clox_test_print_fn, &utest_fixture->printed);
  clox_vm_set_error_handler(&utest_fixture->vm, clox_test_error_handler, &utest_fixture->errors);

  forced_collections = 0;
  clox_vm_define_native(&utest_fixture->vm, "collect", 0, collect_native);
}

UTEST_F_TEARDOWN(gc_run) {
  clox_vm_reset_error_handler(&utest_fixture->vm);
  clox_vm_set_default_print_fn(&utest_fixture->vm);
  clox_vm_free(&utest_fixture->vm);
  clox_compiler_free(&utest_fixture->compiler);
  clox_allocator_free(&utest_fixture->alloc);
}

// Compiles and runs source, which is copied because the compiler modifies the
// buffer it is handed. Reports what the run reported.
static bool run_source(struct gc_run *fixture, const char *source) {
  int written = snprintf(fixture->source, SOURCE_SIZE, "%s", source);
  // a truncated program would still compile and run, and would test something
  // other than what it reads as
  assert(written > 0 && (size_t)written < SOURCE_SIZE);
  (void)written;

  clox_function_t *script = NULL;
  if (!clox_compile(&fixture->compiler, FILE_NAME, fixture->source, &script)) {
    return false;
  }
  return clox_interpret(&fixture->vm, script);
}

UTEST_F(gc_run, a_collection_mid_run_keeps_the_values_on_the_stack) {
  // the two operands are on the stack, above the frame, when the collection
  // between them runs; a swept string would come back as the wrong text
  ASSERT_TRUE(run_source(utest_fixture, "var a = \"le\" + \"ft\";"
                                        "var b = \"ri\" + \"ght\";"
                                        "collect();"
                                        "print a + b;"));

  ASSERT_EQ((size_t)1, forced_collections);
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("leftright", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_collection_mid_run_keeps_the_globals) {
  ASSERT_TRUE(run_source(utest_fixture, "var kept = \"glo\" + \"bal\";"
                                        "collect();"
                                        "print kept;"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("global", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_collection_mid_run_keeps_the_natives) {
  // the natives and the names they are bound to are only ever held by globals
  ASSERT_TRUE(run_source(utest_fixture, "collect();"
                                        "print len(\"abcd\");"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), utest_fixture->printed.values[0]);
}

UTEST_F(gc_run, a_collection_mid_run_keeps_the_active_call_frames) {
  // three frames deep when the collection runs, and every one of them has to
  // come back to code that is still there
  ASSERT_TRUE(run_source(utest_fixture, "fun third() { collect(); return \"th\" + \"ird\"; }"
                                        "fun second() { return third() + \"/second\"; }"
                                        "fun first() { return second() + \"/first\"; }"
                                        "print first();"));

  ASSERT_EQ((size_t)1, forced_collections);
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("third/second/first", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_collection_mid_run_keeps_an_open_upvalue) {
  // captured is still a live local of make when the collection runs, so the
  // upvalue over it is open and the value it points at is on the stack
  ASSERT_TRUE(run_source(utest_fixture, "fun make() {"
                                        "  var captured = \"cap\" + \"tured\";"
                                        "  fun read() { collect(); return captured; }"
                                        "  return read();"
                                        "}"
                                        "print make();"));

  ASSERT_EQ((size_t)1, forced_collections);
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("captured", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_collection_mid_run_keeps_a_closed_upvalue) {
  // make has returned by the time the collection runs, so the value has been
  // moved into the upvalue object and is reachable only through the closure
  ASSERT_TRUE(run_source(utest_fixture, "fun make() {"
                                        "  var captured = \"clo\" + \"sed\";"
                                        "  fun read() { return captured; }"
                                        "  return read;"
                                        "}"
                                        "var reader = make();"
                                        "collect();"
                                        "print reader();"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("closed", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_closure_shared_between_two_holders_survives_losing_one) {
  ASSERT_TRUE(run_source(utest_fixture, "fun make() {"
                                        "  var captured = \"sha\" + \"red\";"
                                        "  fun read() { return captured; }"
                                        "  return read;"
                                        "}"
                                        "var first = make();"
                                        "var second = first;"
                                        "first = nil;"
                                        "collect();"
                                        "print second();"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("shared", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));
}

UTEST_F(gc_run, a_collection_mid_run_reclaims_what_the_program_dropped) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  // The two texts are built at run time and are nothing the program names:
  // a variable's name is itself an interned string, held both as a constant of
  // the chunk and as a key in the globals, so a value spelled like its own
  // variable would stay reachable however the variable was reassigned.
  ASSERT_TRUE(run_source(utest_fixture, "var holder = \"ke\" + \"pt\";"
                                        "var loser = \"gar\" + \"bage\";"
                                        "loser = nil;"
                                        "collect();"
                                        "print holder;"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("kept", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));

  // the collection ran with nothing left referring to the second text, so
  // interning it again has to allocate it afresh
  size_t before = count_objects(alloc);
  (void)clox_string_copy(alloc, "garbage", 7);
  EXPECT_EQ(before + 1, count_objects(alloc));

  // while the text the run kept is still the object the run made
  size_t after = count_objects(alloc);
  (void)clox_string_copy(alloc, "kept", 4);
  EXPECT_EQ(after, count_objects(alloc));
}

// Compiling is the other side of the run: a collection there has to keep the
// function being built, and every function enclosing it.
UTEST_F(gc_run, a_collection_during_compilation_keeps_the_functions_being_built) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  // park bytes so the compiler's own allocations are what cross the threshold
  char *block = NULL;
  size_t bytes = fill_to_threshold(alloc, &block);
  size_t threshold = alloc->next_gc_size;

  ASSERT_TRUE(run_source(utest_fixture, "fun outer() {"
                                        "  fun middle() {"
                                        "    fun inner() { return \"in\" + \"ner\"; }"
                                        "    return inner();"
                                        "  }"
                                        "  return middle();"
                                        "}"
                                        "print outer();"));

  // the threshold moving is what says a collection ran without being asked
  EXPECT_TRUE(alloc->next_gc_size != threshold);
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_STREQ("inner", CLOX_AS_CSTRING(utest_fixture->printed.values[0]));

  CLOX_ARRAY_FREE(alloc, char, block, bytes);
}
