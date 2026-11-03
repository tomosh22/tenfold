static const char* mb_text = "é漢😀"; int /*@mb_after*/mb_after = 1;
/* 😀😀😀😀😀😀😀😀 */ int /*@mb_emoji*/mb_emoji(int);
/* ñ ü 漢字 */ struct /*@mb_Box*/MbBox { int /*@mb_f1*/a; int /*@mb_f2*/b; };
int /*@mb_fn_decl*/mb_fn(const char* s = "😀é漢", int n = 0);
