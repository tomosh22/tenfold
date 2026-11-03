#include "mode.h"

/* C only: `new` and `class` are ordinary identifiers */
static int /*@new_var*/new = 1;
static int /*@class_var*/class = 2;

#ifdef C_DEBUG
int /*@mode_dbg_def*/mode_value(void) { return /*@use_new_dbg*/new; }
#else
long /*@mode_rel_def*/mode_value(void) { return /*@use_class_rel*/class; }
#endif

int main(void) {
    return (int)/*@use_mode*/mode_value() + /*@use_as_cpp*/as_cpp_fn(1) + /*@use_as_c*/as_c_fn(2) + /*@use_new_main*/new + /*@use_class_main*/class;
}
