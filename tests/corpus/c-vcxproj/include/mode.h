#ifndef MODE_H
#define MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* the type of mode_value() depends on the active configuration */
#ifdef C_DEBUG
int /*@mode_dbg*/mode_value(void);
#else
long /*@mode_rel*/mode_value(void);
#endif

int /*@as_cpp_fn_decl*/as_cpp_fn(int v);   /* defined in as_cpp.c, compiled as C++ */
int /*@as_c_fn_decl*/as_c_fn(int v);       /* defined in as_c.cpp, compiled as C */

#ifdef __cplusplus
}
#endif

#endif
