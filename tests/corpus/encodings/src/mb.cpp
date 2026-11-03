#include "mb.h"

int /*@mb_emoji_def*/mb_emoji(int v) { return v; }
int /*@mb_fn_def*/mb_fn(const char* s, int n) { MbBox m{1, 2}; return /*@use_mb_emoji_in*/mb_emoji(n) + (int)s[0] + m./*@use_mb_f1_in*/a + /* 😀 */ m./*@use_mb_f2_in*/b + /*@use_mb_after_in*/mb_after; }
