#include "declare.h"

/*@decl_gadget*/DECLARE_CLASS(/*@Gadget*/Gadget)
MAKE_FUNC(/*@compute_a*/compute_a, 1)
MAKE_FUNC(/*@compute_b*/compute_b, /*@use_compute_a_in_b*/compute_a() + 1)
DECLARE_ENUM(/*@Hue*/Hue, /*@Hue_Warm*/Warm, /*@Hue_Cold*/Cold)
GEN_NAME_ARG(/*@gen_fn*/gen_fn)

class /*@Holder*/Holder {
 public:
  /*@fld_count_decl*/DECLARE_FIELD(int, /*@fld_count_arg*/count)
  /*@getter_count_decl*/DEFINE_GETTER(int, /*@getter_count_arg*/count)
  /*@prop_size_decl*/PROPERTY(int, /*@prop_size_arg*/size)
  int /*@Holder_plain*/plain_member = 0;
};

class Derived : public /*@use_Holder_base*/Holder {
 public:
  int total() const { return /*@use_get_count_in_derived*/get_count() + /*@use_size_call_in_derived*/size(); }
};

void exercise() {
  /*@use_Gadget*/Gadget g;
  g./*@use_value*/value = /*@use_compute_b*/compute_b();
  /*@use_Hue*/Hue h = /*@use_Hue_scope*/Hue::/*@use_warm*/Warm;
  (void)h;
  /*@use_Holder_local*/Holder holder;
  holder./*@use_count_field*/count_ = 3;
  int n = holder./*@use_get_count*/get_count();
  holder./*@use_set_size*/set_size(n);
  int s = holder./*@use_size*/size();
  holder./*@use_plain*/plain_member = s + /*@use_gen_fn*/gen_fn(1);
}
