#include "basic.h"

int /*@arr_len*/arr_len = /*@use_max_items*/MAX_ITEMS;
int sq_value = /*@use_square*/SQUARE(3);
int sum_value = /*@use_add3*/ADD3(1, 2, 3);
int twice_value = /*@use_twice*/TWICE_SQ(4);
const char* xstr_value = /*@use_xstr*/XSTR(/*@use_max_in_xstr*/MAX_ITEMS);
const char* str_value = /*@use_str*/STR(MAX_ITEMS);
int alias_value = /*@use_alias*/ALIAS_MAX;
double pi_value = /*@use_pi*/PI_VALUE;
int via_func = /*@use_helper_sum*/helper_sum(/*@use_max_in_arg*/MAX_ITEMS, 1);
int nested_args = /*@use_square_outer*/SQUARE(/*@use_add3_inner*/ADD3(1, /*@use_helper_in_macro_arg*/helper_sum(1, 2), 3));

// A function-like macro and a function with the same name: only "name(" expands the macro.
int (/*@clamp01_fn*/clamp01)(int v) { return v < 0 ? 0 : v; }
int clamped_macro = /*@use_clamp01_macro*/clamp01(5);
int clamped_paren = (/*@use_clamp01_paren*/clamp01)(5);
int (*clamp_ptr)(int) = /*@use_clamp01_ptr*/clamp01;

// An object-like macro name used as a member name is not a use of the macro when it is not defined.
int item_id(Item& it) { return it./*@use_item_id*/id; }

// Names are pasted: the declared variable is var_one.
int /*@catv*/CAT(var_, one) = 1;
int use_var_one = /*@use_var_one*/var_one;
int /*@catv2*/CAT_EXP(var_, /*@arg_two*/two) = 2;
int use_var_two = /*@use_var_two*/var_two;
