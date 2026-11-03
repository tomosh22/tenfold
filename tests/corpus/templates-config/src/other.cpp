#include "policy.h"

int other() { return Policy<int>::/*@use2_run_int*/run(5) + /*@use2_cost*/cost<char>(); }
