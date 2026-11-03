#include "dup.h"
// other/ has no dup.h: the quoted include falls back to the -I list, inc1 first.
int a = /*@use_dup_other*/dup_value();
