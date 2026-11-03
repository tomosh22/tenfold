#pragma once
struct /*@crlf_Box*/CrlfBox {
    int /*@crlf_count*/count;
};
int /*@crlf_fn_decl*/crlf_fn(const CrlfBox& b);
/* a block comment
   spanning lines */
#define /*@CRLF_MAX*/CRLF_MAX(a, b) \
    ((a) > (b) ? (a) : (b))
int /*@crlf_after_macro*/crlf_after_macro(int v);
