#include "mode.h"
int a = /*@use_impl_slow*/impl_mode();
int b = /*@use_common_slow*/common_mode();
/*@use_SlowOnly*/SlowOnly so = { 1 };
int c = so./*@use_SlowOnly_v*/v;
