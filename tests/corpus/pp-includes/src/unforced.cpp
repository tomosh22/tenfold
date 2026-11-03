// Same shape, but force.h is not forced here.
#ifdef /*@ifdef_unforced*/FORCED_MACRO
int /*@u_branch_forced*/u_branch_marker = 1;
#else
int /*@u_branch_unforced*/u_branch_marker = 0;
#endif
int d = /*@use_u_branch*/u_branch_marker;
