#include "legacy.hh"

int /*@legacy_mode_def*/legacy_mode = 1;
int /*@legacy_add_def*/legacy_add(int a, int b) { return a + b + /*@use_legacy_mode*/legacy_mode; }
