#include "inherit.h"
Derived::/*@Derived_ctor_cstr_def*/Derived(const char*) : /*@use_Base_init*/Base(0) {}
Derived::/*@Derived_dtor_def*/~Derived() {}
