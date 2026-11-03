#include "fields.h"

struct /*@Rect*/Rect {
  /*@fields_decl_use*/FIELDS(/*@use_DECL_FIELD*/DECL_FIELD)
  /*@Rect_ctor*/Rect() : /*@fields_init_use*/FIELDS(INIT_FIELD) /*@init_marker*/marker(0) {}
  int /*@Rect_marker*/marker;
  static constexpr int /*@Rect_count*/count = 0 /*@fields_count_use*/FIELDS(COUNT_FIELD);
};

int area(const Rect& r) { return r./*@use_width*/width * r./*@use_height*/height; }
double ratio_of(const Rect& r) { return r./*@use_ratio*/ratio; }
int marker_of(const Rect& r) { return r./*@use_marker*/marker + /*@use_Rect_scope*/Rect::/*@use_count*/count; }
