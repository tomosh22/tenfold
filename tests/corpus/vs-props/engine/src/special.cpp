// Per-file metadata: SPECIAL_FILE and special_inc exist for this file only.
#ifdef SPECIAL_FILE
#include "special_only.h"
int a = /*@use_special_only*/special_only();
int /*@special_yes*/special_marker = 1;
#else
int /*@special_no*/special_marker = 0;
#endif
int b = /*@use_special_marker*/special_marker;
