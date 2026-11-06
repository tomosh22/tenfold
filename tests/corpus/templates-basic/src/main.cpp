#include <map>
#include <string>
#include <vector>
#include "tb/algo.h"
#include "tb/pack.h"
#include "tb/queue.h"
#include "tb/stack.h"
#include "tb/types.h"

using namespace tb;

int main() {
  // ---- class template, members defined out of line
  /*@use_Stack*/Stack<int> s;
  s./*@use_push*/push(1);
  int one = 1;
  s./*@use_push_lv*/push(one);
  s./*@use_push_rv*/push(std::move(one));
  s./*@use_top*/top() = 2;
  const Stack<int>& cs = s;
  int t = cs./*@use_top_c*/top();
  std::vector<int> src{1, 2, 3};
  s./*@use_push_all*/push_all(src);
  Stack<int>::/*@use_value_type*/value_type vt = 0;
  Stack<int>::/*@use_Node*/Node nd{3};
  int nv = nd./*@use_Node_value*/value;
  Stack<int>::/*@use_live*/live = 1;
  Stack<int>::/*@use_made*/made = 2;
  // chained calls, return type computed from a lambda
  auto chained = s./*@use_map1*/map([](int x) { return x * 2.0; })./*@use_map2*/map([](double d) { return d > 1; })./*@use_size_chain*/size();
  // ---- a template whose .inl is self-contained
  Queue<int> q;
  q./*@use_q_push*/push(1);
  int qv = q./*@use_q_pop*/pop();
  bool qe = q./*@use_q_empty*/empty();
  (void)qv; (void)qe;
  // ---- function templates in .inl
  apply_each(src.begin(), src.end(), [](int) {});
  /*@use_apply_each*/apply_each(src.begin(), src.end(), [](int) {});
  int cl = /*@use_clamp*/clamp_to(5, 0, 3);
  auto tw = /*@use_twice*/twice(2.5);
  auto ad = /*@use_add_tr*/add(1, 2.5);
  decltype(auto) fr = /*@use_first_ref*/first_ref(src);
  constexpr int f5 = /*@use_fact*/fact(5);
  int sq = /*@use_square*/square(4);
  // ---- variadics
  int ca = /*@use_count_args*/count_args(1, "a", 2.0);
  auto sf = /*@use_sum_fold*/sum_fold(1, 2, 3);
  bool af = /*@use_all_of_fold*/all_of_fold(true, false);
  int cw = /*@use_call_with*/call_with([](int a, int b) { return a + b; }, 1, 2);
  int vm1 = /*@use_vmax_base*/vmax(3);
  int vm2 = /*@use_vmax_rec*/vmax(3, 4, 5);
  constexpr auto ts = TypeList</*@use_Ts_int*/int, double>::/*@use_TypeList_size*/size;
  IntSeq<1, 2, 3> iseq;
  int ln = /*@use_IntSeq*/IntSeq<4>::/*@use_IntSeq_len*/len;
  // ---- dependent names (the template is never instantiated with a clever type)
  dependent_names(Box<int>{7});
  // ---- CTAD, deduction guides
  /*@use_Pair*/Pair /*@var_p*/p{1, 2.0};
  /*@use_Pair_explicit*/Pair<int, double> p2(1, 2.0);
  auto first = p./*@use_Pair_first*/first;
  /*@use_Wrap*/Wrap /*@var_w*/w(src.begin(), src.end());
  /*@use_Wrap_explicit*/Wrap<int> w2(src.begin(), src.end());
  decltype(p) dp = p;
  // ---- alias / variable templates, template template parameters
  /*@use_Vec_t*/Vec_t<int> vi;
  /*@use_Map_str*/Map_str<double> md;
  bool small = /*@use_is_small_v*/is_small_v<int>;
  double pd = /*@use_pi_v*/pi_v<double>;
  /*@use_Holder*/Holder<std::vector, int> hv;
  hv./*@use_Holder_items*/items.push_back(1);
  // ---- nested template arguments: a<b<c>>
  std::map<std::string, std::vector</*@use_Pair_nested*/Pair<int, /*@use_Box_nested*/Box<double>>>> nested;
  Vec_t<Map_str<Vec_t</*@use_Wrap_nested*/Wrap<int>>>> deep;
  (void)t; (void)vt; (void)nv; (void)chained; (void)cl; (void)tw; (void)ad; (void)fr; (void)f5; (void)sq; (void)ca;
  (void)sf; (void)af; (void)cw; (void)vm1; (void)vm2; (void)ts; (void)ln; (void)p2; (void)first; (void)w; (void)dp;
  (void)small; (void)pd; (void)nested; (void)deep; (void)iseq; (void)md; (void)vi;
  return 0;
}
