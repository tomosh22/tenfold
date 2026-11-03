#include "mode.h"
int a = /*@use_impl_fast*/impl_mode();
int b = /*@use_common_fast*/common_mode();
/*@use_FastOnly*/FastOnly fo = { 1 };
int c = fo./*@use_FastOnly_v*/v;
