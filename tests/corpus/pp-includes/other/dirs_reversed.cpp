#include "dup.h"
// Compiled with -Iinc2 -Iinc1: inc2/dup.h wins.
int a = /*@use_dup_reversed*/dup_value();
