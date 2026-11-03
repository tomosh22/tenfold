#include "b/widget.h"
#include "b/node.h"
struct b::Widget::/*@b_Impl*/Impl {
  int /*@b_Impl_get*/get() const { return node./*@use_b_Node_get_in_Impl*/get(); }
  int /*@b_Impl_size*/size() const { return node./*@use_b_Node_size_in_Impl*/size(); }
  b::Node node;
};
b::Widget::/*@b_Widget_ctor_def*/Widget() : impl_(new /*@new_b_Impl*/Impl) {}
b::Widget::/*@b_Widget_dtor_def*/~Widget() { delete impl_; }
int b::Widget::/*@b_Widget_get_def*/get() const { return impl_->/*@use_b_Impl_get*/get(); }
int b::Widget::/*@b_Widget_size_def*/size() const { return impl_->/*@use_b_Impl_size*/size() + b::/*@use_b_get_free*/get(&impl_->node); }
