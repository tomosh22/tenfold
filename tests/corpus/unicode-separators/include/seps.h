#pragma once
// NEL, LS and PS are line breaks in the editor, as in Visual Studio. clang and clangd do not know them, so every
// position after one has to be converted; a line comment goes on past them (nothing after one is code). int /*@hidden_ls*/hidden_ls();
int /*@ls_decl*/ls_fn(int v); int /*@ps_decl*/ps_fn(int v);
/* café 😀 block comment ends here */ int /*@nel_decl*/nel_fn(int v);
struct /*@Box*/Box { int /*@Box_w*/w;  int /*@Box_h*/h; };
extern int /*@ps_var_decl*/ps_var;
