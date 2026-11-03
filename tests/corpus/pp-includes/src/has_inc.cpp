#if __has_include("local_override.h")
#include "local_override.h"
#else
#include "default_impl.h"
#endif
#if __has_include(<only_in_inc2.h>)
#include <only_in_inc2.h>
int /*@inc2_branch*/inc2_marker = /*@use_only_inc2_init*/only_inc2_fn();
#else
int /*@no_inc2_branch*/no_inc2_marker = 0;
#endif
int a = /*@use_impl_choice*/impl_choice();
int b = /*@use_only_inc2*/only_inc2_fn() + /*@use_inc2_marker*/inc2_marker;
