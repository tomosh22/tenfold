/* a .cpp file that the project compiles as C (CompileAs=CompileAsC) */
#include "mode.h"

static int /*@asC_delete*/delete = 5;
int /*@as_c_fn_def*/as_c_fn(int v) { return v + /*@use_asC_delete*/delete; }
