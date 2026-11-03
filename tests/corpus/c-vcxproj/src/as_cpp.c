/* a .c file that the project compiles as C++ (CompileAs=CompileAsCpp) */
#include "mode.h"

class /*@AsCpp*/AsCpp {
public:
    int /*@asc_m*/m;
    AsCpp(int v) : /*@use_asc_m_init*/m(v) {}
    int /*@asc_get*/get() const { return /*@use_asc_m*/m; }
};

int /*@as_cpp_fn_def*/as_cpp_fn(int v) { AsCpp a(v); return a./*@use_asc_get*/get(); }
