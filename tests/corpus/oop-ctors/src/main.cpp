#include "widget.h"
#include "inherit.h"
#include "convert.h"
#include "enums.h"
#include "bits.h"
#include "registry.h"
#include "containers.h"

double /*@take_feet_def*/take_feet(Feet f) { return f.v; }
double /*@take_meters_def*/take_meters(Meters m) { return m.v; }

int widgets() {
  Widget a;
  Widget b(3);
  Widget c(3, 4);
  Widget d = a;
  Widget e = static_cast<Widget&&>(c);
  a = b;
  b = static_cast<Widget&&>(d);
  Widget f = Widget::/*@use_make*/make(2);
  Widget* g = new /*@use_new_int*/Widget(5);
  int n = static_cast<int>(f) + f./*@use_Widget_size*/size();
  n += f./*@use_explicit_to_int*/operator int();
  if (f./*@use_explicit_to_bool*/operator bool()) ++n;
  n += Widget::/*@use_live*/live;
  n += f./*@use_Widget_get*/get();
  g->/*@use_dtor_call*/~Widget();
  Widget h = /*@use_temp_int*/Widget(2);
  Widget i = /*@use_temp_wh*/Widget(2, 3);
  Widget j{4};
  (void)e; (void)h; (void)i; (void)j;
  delete g;
  return n;
}

int inherit() {
  Derived d1(1);
  Derived d2(1, 2);
  Derived d3("x");
  Leaf leaf(3);
  Base* bp = &leaf;
  return d1./*@use_Derived_get*/get() + bp->/*@use_Base_get*/get() + leaf./*@use_leaf_tag*/tag + d2.v_ + d3.extra
       + /*@use_Derived_temp*/Derived(1).extra + /*@use_Derived_temp2*/Derived(1, 2).extra;
}

double convert() {
  Meters /*@var_m*/m(2.0);
  Feet /*@var_f*/f = /*@conv_m_init*/m;
  Meters back = f./*@use_Feet_to_Meters*/operator Meters();
  double x = f./*@use_Feet_to_double*/operator double();
  return /*@use_take_feet*/take_feet(/*@conv_m_arg*/m) + /*@use_take_meters*/take_meters(/*@conv_f_arg*/f) + x + back.v;
}

int enums() {
  Color c = /*@use_Red*/Red;
  Mode m = Mode::/*@use_Mode_Red*/Red;
  Level l = /*@use_Low*/Low;
  ui::Color u = ui::Color::/*@use_ui_Red*/Red;
  switch (c) {
    case /*@case_Green*/Green: break;
    case /*@case_Blue*/Blue: break;
    default: break;
  }
  switch (m) {
    case Mode::/*@case_Mode_Fast*/Fast: break;
    case Mode::/*@case_Mode_Blue*/Blue: break;
    default: break;
  }
  {
    using enum Mode;
    Mode m2 = /*@use_using_enum_Fast*/Fast;
    (void)m2;
  }
  Color /*@var_c2*/c2 = static_cast<Color>(1);
  (void)u; (void)l; (void)c2;
  return static_cast<int>(c) + static_cast<int>(m);
}

int bits() {
  Flags fl{};
  fl./*@use_Flags_b*/b = 5;
  fl./*@use_Flags_c*/c = 2;
  Variant v;
  v./*@use_Variant_i*/i = 3;
  Tagged t;
  t.kind = Tagged::/*@use_Tagged_Int*/IntKind;
  t./*@use_Tagged_i*/i = 4;
  t./*@use_Tagged_d*/d = 1.5;
  return fl./*@use_Flags_b2*/b + v./*@use_Variant_f*/f + t./*@use_Tagged_i2*/i;
}

int statics() {
  Registry::/*@use_Registry_add*/add(1);
  ++Registry::/*@use_Registry_count*/count;
  Registry& r = Registry::/*@use_Registry_instance*/instance();
  int arr[Registry::/*@use_Registry_max*/max];
  (void)arr;
  return r./*@use_Registry_get*/get() + Registry::/*@use_Registry_count2*/count;
}

int containers() {
  List /*@var_list*/list;
  int total = 0;
  for (List::/*@use_List_Node_in_for*/Node& n : /*@range_list*/list) total += n./*@use_List_Node_get*/get();
  List::/*@use_List_Node_ln*/Node ln;
  Tree::/*@use_Tree_Node_tn*/Node tn;
  /*@use_Node_gn*/Node gn;
  struct /*@Local*/Local {
    int /*@Local_get*/get() const { return 5; }
    int /*@Local_size*/size() const { return 6; }
  };
  /*@use_Local*/Local loc;
  total += ln./*@use_List_Node_get2*/get() + tn./*@use_Tree_Node_get*/get() + gn./*@use_Node_get*/get() + loc./*@use_Local_get*/get() + loc./*@use_Local_size*/size();
  total += list./*@use_List_size*/size();
  return total;
}
