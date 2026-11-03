#include "using_hdr.h"
#include "versions.h"

namespace /*@dee_alias*/dee = deep::er::est;
namespace /*@oi_alias*/oi = outer::inner;

int use_all() {
  int a = outer::/*@use_outer_value*/value();
  int b = outer::inner::/*@use_inner_value*/value();
  int c = /*@use_oi*/oi::/*@use_oi_value*/value();
  /*@use_dee*/dee::/*@use_Thing*/Thing /*@var_t*/t;
  int d = t./*@use_Thing_size*/size();
  int e = /*@use_dee2*/dee::/*@use_make*/make();
  int f = api::/*@use_call*/call(1);
  int g = api::v2::/*@use_call_v2*/call(1);
  api::/*@use_Opts*/Opts o;
  api::v2::/*@use_v2_Opts*/Opts o2;
  int h = app::/*@use_app_value*/value();
  app::/*@use_app_Node*/Node /*@var_n*/n;
  int i = n./*@use_Node_get*/get();
  int s = /*@use_global_value*/value(3);
  {
    using namespace outer::inner;
    /*@use_dir_Node*/Node /*@var_m*/m;
    int j = m./*@use_dir_get*/get();
    int k = /*@use_dir_value*/value();
    (void)j; (void)k;
  }
  return a + b + c + d + e + f + g + h + i + s + o.level + o2.extra;
}
