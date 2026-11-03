// Object-like and function-like macros, macros calling macros, stringising, pasting.
#pragma once
#define /*@MAX_ITEMS*/MAX_ITEMS 16
#define /*@SQUARE*/SQUARE(x) ((x) * (x))
#define /*@ADD3*/ADD3(a, b, c) ((a) + (b) + (c))
#define /*@TWICE_SQ*/TWICE_SQ(v) (2 * /*@sq_in_twice*/SQUARE(v))
#define /*@STR*/STR(x) #x
#define /*@XSTR*/XSTR(x) /*@str_in_xstr*/STR(x)
#define /*@CAT*/CAT(a, b) a##b
#define /*@CAT_EXP*/CAT_EXP(a, b) /*@cat_in_catexp*/CAT(a, b)
#define /*@PI_VALUE*/PI_VALUE 3.14159
#define /*@ALIAS_MAX*/ALIAS_MAX /*@max_in_alias*/MAX_ITEMS
#define /*@clamp01*/clamp01(v) ((v) < 0 ? 0 : ((v) > 1 ? 1 : (v)))

struct Item {
  int /*@Item_id*/id;
};

inline int /*@helper_sum*/helper_sum(int a, int b) { return a + b; }
