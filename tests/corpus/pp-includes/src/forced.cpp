// No #include: force.h arrives through -include on the command line.
int a = /*@use_forced_fn*/forced_fn();
int b = /*@use_FORCED_MACRO*/FORCED_MACRO;
/*@use_ForcedType*/ForcedType ft = { 3 };
int c = ft./*@use_ForcedType_v*/v;
#ifdef /*@ifdef_forced*/FORCED_MACRO
int /*@branch_forced*/branch_marker = 1;
#else
int /*@branch_unforced*/branch_marker = 0;
#endif
int d = /*@use_branch_forced*/branch_marker;
