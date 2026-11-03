#include <dup.h>
// <dup.h> never looks in the including file's directory: first -I wins, inc1/dup.h.
int a = /*@use_dup_angle*/dup_value();
