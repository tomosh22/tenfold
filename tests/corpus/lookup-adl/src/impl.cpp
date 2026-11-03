#include "vec.h"
#include "shape.h"
#include "swap.h"
#include "hide.h"
#include "friends.h"

vec::V vec::/*@vec_scale_def*/scale(V v, int k) { return V{v.x * k}; }
bool vec::/*@vec_eq_def*/operator==(const V& a, const V& b) { return a.x == b.x; }
io::Out& vec::/*@vec_shl_def*/operator<<(io::Out& o, const V& v) { return o /*@use_shl_int2*/<< v.x; }
void shape::/*@shape_draw_circle_def*/draw(const Circle&) {}
void shape::/*@shape_draw_square_def*/draw(const Square&) {}
void other::/*@other_draw_def*/draw(const shape::Circle&) {}
void /*@global_draw_def*/draw(int) {}
void adl_swap::/*@w_swap_def*/swap(W& a, W& b) { int t = a.v; a.v = b.v; b.v = t; }
int reach::/*@a_fa_def*/fa() { return 1; }
int via::/*@via_fb_def*/fb() { return 2; }
void /*@g_f_def*/f(int) {}
int /*@g_count_def*/count = 5;
void Base::/*@Base_f_int_def*/f(int) {}
void Base::/*@Base_f_double_def*/f(double) {}
void /*@audit_def2*/audit(Bank& b) { (void)b./*@use_secret_audit*/secret; }
