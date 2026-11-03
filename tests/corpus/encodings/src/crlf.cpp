#include "crlf.h"

int /*@crlf_fn_def*/crlf_fn(const CrlfBox& b) {
    const char* raw = R"(line one
line two)";
    return b./*@crlf_count_def*/count + (int)raw[0];
}

int /*@crlf_after_macro_def*/crlf_after_macro(int v) { return /*@use_CRLF_MAX_in*/CRLF_MAX(v, 2); }
