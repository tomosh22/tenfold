#include <string>
#include <vector>
#include "spec/convert.h"
#include "spec/crtp.h"
#include "spec/enable.h"
#include "spec/printer.h"
#include "spec/registry.h"
#include "spec/virt.h"

using namespace spec;

int main() {
  // ---- class template specialisations
  /*@use_Printer_int*/Printer<int> /*@var_pi*/pi;
  /*@use_Printer_ptr*/Printer<char*> /*@var_pp*/pp;
  /*@use_Printer_vec*/Printer<std::vector<int>> /*@var_pv*/pv;
  /*@use_Printer_dbl*/Printer<double> /*@var_pd*/pd;
  std::string n0 = Printer<int>::/*@use_name_int*/name();
  std::string n1 = Printer<char*>::/*@use_name_ptr*/name();
  std::string n2 = Printer<std::vector<int>>::/*@use_name_vec*/name();
  std::string n3 = Printer<double>::/*@use_name_dbl*/name();
  int e1 = Printer<int>::/*@use_extra*/extra();
  int r1 = Printer<std::vector<int>>::/*@use_rank_vec*/rank();
  int r2 = Printer<double>::/*@use_rank_dbl*/rank();
  Printer<char*>::/*@use_pointee*/pointee ch = 0;
  // ---- variable templates
  int v0 = /*@use_id_v_dbl*/id_v<double>;
  int v1 = /*@use_id_v_ptr*/id_v<int*>;
  int v2 = /*@use_id_v_int*/id_v<int>;
  // ---- detection idiom
  bool h1 = /*@use_has_size_vec*/has_size<std::vector<int>>::value;
  bool h2 = /*@use_has_size_int*/has_size<int>::value;
  // ---- function template specialisation vs. overload vs. primary
  int a = 1, b = 2;
  double x = 1, y = 2;
  /*@use_swap_int*/swap_it(a, b);
  /*@use_swap_dbl*/swap_it(x, y);
  std::string c1 = /*@use_convert_bool*/convert(true);
  std::string c2 = /*@use_convert_int*/convert(3);
  std::string c3 = /*@use_convert_cstr*/convert("s");
  std::string c4 = /*@use_convert_expl*/convert<float>(1.0f);
  std::string c5 = /*@use_checked*/convert_checked(3);
  Conv<int> cv;
  Conv<int, double> cv2;
  double cvd = cv2./*@use_Conv_apply*/apply(1);
  // ---- explicit instantiation and member specialisation
  /*@use_Registry_int*/Registry<int> ri;
  Registry<double> rd;
  ri./*@use_reg_add_int*/add(1);
  rd./*@use_reg_add_dbl*/add(1.0);
  int w1 = ri./*@use_weight_int*/weight();
  int w2 = rd./*@use_weight_dbl*/weight();
  double asd = ri./*@use_as_double*/as<double>();
  long asl = ri./*@use_as_long*/as<long>();
  // ---- CRTP
  Circle /*@var_circle*/c;
  double ar = c./*@use_circle_area*/area();
  double sc = c./*@use_scaled*/scaled(2.0);
  Square sq;
  double sqa = sq./*@use_square_area*/area();
  double sqs = sq./*@use_square_scaled*/scaled(2.0);
  double tot = /*@use_total_area*/total_area(c);
  double cr = c./*@use_circle_r*/r;
  Widget /*@var_widget*/wg;
  int lv = Widget::/*@use_live*/live;
  // ---- virtual functions of a class template
  IntTask it;
  Task<int>& tref = it;
  int tr = tref./*@use_task_run*/run();
  int tn2 = tref./*@use_task_name*/name();
  FloatTask ft;
  Task<float>& tfref = ft;
  float tfr = tfref./*@use_task_run_f*/run();
  (void)tr; (void)tn2; (void)tfr;
  // ---- SFINAE
  int hi = /*@use_half_int*/half(4);
  double hf = /*@use_half_fp*/half(4.0);
  int k1 = /*@use_kind_ptr*/kind(&a);
  int k2 = /*@use_kind_val*/kind(a);
  int dd = /*@use_dispatch*/dispatch(3);
  auto dsc = /*@use_describe*/describe(3.0);
  (void)pi; (void)pp; (void)pv; (void)pd; (void)n0; (void)n1; (void)n2; (void)n3; (void)e1; (void)r1; (void)r2;
  (void)ch; (void)v0; (void)v1; (void)v2; (void)h1; (void)h2; (void)c1; (void)c2; (void)c3; (void)c4; (void)c5;
  (void)cv; (void)cvd; (void)w1; (void)w2; (void)asd; (void)asl; (void)ar; (void)sc; (void)sqa; (void)sqs; (void)tot;
  (void)cr; (void)wg; (void)lv; (void)hi; (void)hf; (void)k1; (void)k2; (void)dd; (void)dsc;
  return 0;
}
