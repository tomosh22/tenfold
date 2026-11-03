#include "stack.h"
#include <widgets>

int run() {
  /*@use_Stack*/Stack s;
  s./*@use_push*/push(1);
  int a = s./*@use_top*/top() + s./*@use_size*/size();
  bool e = s./*@use_empty*/empty();
  s./*@use_count*/count_ = 2;
  (void)e;
  /*@use_Widget*/Widget w = { /*@use_make_widget_id*/make_widget_id() };
  return a + w./*@use_Widget_id*/id;
}
