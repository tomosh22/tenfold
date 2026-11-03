#include "dup.h"
#include <next.h>
// "dup.h" from src/ is src/dup.h even though -Iinc1 would also match.
int a = /*@use_dup_quoted*/dup_value();
int b = /*@use_next_a*/next_a() + /*@use_next_b*/next_b();
