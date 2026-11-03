// The same headers reached through several spellings: only the first inclusion declares anything.
#include "guard.h"
#include "../include/guard.h"
#include "../include/./guard.h"
#include <once.h>
#include "once.h"
#include "../include/once.h"
#include "rel/inner.h"

int g = /*@use_guarded_fn*/guarded_fn();
int o = /*@use_once_fn*/once_fn();
/*@use_GuardBox*/GuardBox gb = { 1 };
/*@use_OnceBox*/OnceBox ob = { 2 };
int gv = gb./*@use_GuardBox_v*/v + ob./*@use_OnceBox_v*/v;
int inner_sum = /*@use_inner_fn*/inner_fn() + /*@use_sibling_fn*/sibling_fn() + /*@use_peer_fn*/peer_fn();
