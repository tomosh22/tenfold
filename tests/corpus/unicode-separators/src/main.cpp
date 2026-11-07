#include "seps.h"
/* a block comment that ends here */ int /*@ls_fn_def*/ls_fn(int v) { return v + 1; }
// éé line comment int hidden_ps = 3;
int /*@ps_fn_def*/ps_fn(int v) { return v + 2; } int /*@nel_fn_def*/nel_fn(int v) { return v + 3; }int /*@ps_var_def*/ps_var = 5;

int main() {
  Box b;
  int a = /*@use_ls_fn*/ls_fn(1);   int c = /*@use_nel_fn*/nel_fn(a);
  b./*@use_Box_w*/w = /*@use_ps_fn*/ps_fn(c);   b./*@use_Box_h*/h = /*@use_ps_var*/ps_var;
  return b.w + b./*@use_Box_h2*/h;
}
