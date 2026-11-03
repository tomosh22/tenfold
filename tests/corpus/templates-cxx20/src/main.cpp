#include <string>
#include <vector>
#include "cx/functions.h"
#include "cx/types.h"

using namespace cx;

static_assert(/*@use_Numeric_sa*/Numeric<int>);
static_assert(!Numeric<std::string>);

template <class T>
  requires /*@use_Numeric_req*/Numeric<T> && (sizeof(T) >= 2)
T /*@wide_only*/wide_only(T v) { return v; }

template <class T>
int /*@dispatch*/dispatch(T v) {
  if constexpr (/*@use_Container_ifc*/Container<T>) return describe(v);
  else return /*@use_describe_in_tmpl*/describe(v);
}

template <Pointerish P, /*@use_Printable_tp*/Printable V>
V /*@fetch*/fetch(P p, V fallback) { return p ? *p : fallback; }

int main() {
  // ---- constrained function templates
  int a = /*@use_square*/square(3);
  double b = /*@use_cube*/cube(1.5);
  long c = /*@use_quad*/quad(2L);
  auto d = /*@use_halve*/halve(9);
  auto e = /*@use_combine*/combine(1, 2.5);
  short f = /*@use_wide_only*/wide_only<short>(1);
  // ---- constraint-ordered overloads
  std::vector<int> v{1, 2};
  std::string s = "x";
  int o1 = /*@use_describe_num*/describe(1);
  int o2 = /*@use_describe_sized*/describe(v);
  int o3 = /*@use_describe_sized2*/describe(s);
  int o4 = /*@use_describe_any*/describe(nullptr);
  int o5 = /*@use_dispatch*/dispatch(2.5);
  bool sb = /*@use_same_bytes*/same_bytes(1, 2u);
  // ---- partial specialisation by constraint
  int k0 = Holder<std::string>::/*@use_kind_primary*/kind;
  int k1 = Holder<int>::/*@use_kind_num*/kind;
  int k2 = Holder<char*>::/*@use_kind_ptr*/kind;
  /*@use_Holder_num*/Holder<double> hd;
  // ---- constrained members
  Buf<int> bi{3};
  Buf<std::string> bs;
  int g1 = bi./*@use_get_num*/get();
  std::string g2 = bs./*@use_get_other*/get();
  // ---- NTTP
  /*@use_Tag*/Tag<Fixed{4}> tag;
  int tn = Tag<Fixed{4}>::/*@use_Tag_n*/n;
  int cv = /*@use_Const*/Const<7>::/*@use_Const_value*/value;
  // ---- evaluation
  constexpr int len = /*@use_ce_len*/ce_len("abcd");
  constexpr int p8 = /*@use_cx_pow*/cx_pow(2, 3);
  int gc = /*@use_g_counter*/g_counter;
  // ---- aggregate CTAD, designated initialisers, using enum
  /*@use_Triple*/Triple /*@var_t*/t{1, 2, 3};
  int ta = t./*@use_Triple_a*/a;
  Config cfg{./*@use_port*/port = 80, ./*@use_tls*/tls = true};
  using enum Color;
  Color col = /*@use_Red*/Red;
  Color col2 = Color::/*@use_Green*/Green;
  // ---- concept in a nested requires-expression
  constexpr bool ok = requires(int x) { /*@use_square_in_req*/square(x); };
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)o1; (void)o2; (void)o3; (void)o4; (void)o5; (void)sb;
  (void)k0; (void)k1; (void)k2; (void)hd; (void)g1; (void)g2; (void)tag; (void)tn; (void)cv; (void)len; (void)p8;
  (void)gc; (void)ta; (void)cfg; (void)col; (void)col2; (void)ok;
  return 0;
}
