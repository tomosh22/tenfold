#include "widget.h"
int Widget::/*@Widget_live_def*/live = 0;
Widget::/*@Widget_ctor0_def*/Widget() : /*@use_deleg*/Widget(1) {}
Widget::/*@Widget_ctor_int_def*/Widget(int size) : size_(size) { ++/*@use_live_in_ctor*/live; }
Widget::/*@Widget_ctor_wh_def*/Widget(int w, int h) : /*@use_deleg_wh*/Widget(w * h) {}
Widget::/*@Widget_copy_def*/Widget(const Widget& o) : size_(o.size_) { ++/*@use_live_copy*/live; }
Widget::/*@Widget_move_def*/Widget(Widget&& o) noexcept : size_(o.size_) { ++/*@use_live_move*/live; }
Widget& Widget::/*@Widget_copy_assign_def*/operator=(const Widget& o) { size_ = o.size_; return *this; }
Widget& Widget::/*@Widget_move_assign_def*/operator=(Widget&& o) noexcept { size_ = o.size_; return *this; }
Widget::/*@Widget_dtor_def*/~Widget() { --/*@use_live_dtor*/live; }
Widget::/*@Widget_to_bool_def*/operator bool() const { return size_ != 0; }
Widget::/*@Widget_to_int_def*/operator int() const { return size_; }
int Widget::/*@Widget_get_def*/get() const { return size_; }
Widget Widget::/*@Widget_make_def*/make(int n) { return Widget(n); }
