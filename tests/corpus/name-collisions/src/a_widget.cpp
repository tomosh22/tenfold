#include "a/widget.h"
#include "a/node.h"
namespace a {
class /*@a_Impl*/Impl {
 public:
  int /*@a_Impl_get*/get() const { return node./*@use_a_Node_get_in_Impl*/get(); }
  int /*@a_Impl_size*/size() const { return a::/*@use_a_size_free*/size(node); }
  /*@use_a_Node_in_Impl*/Node node;
};
Widget::/*@a_Widget_ctor_def*/Widget() : impl_(new /*@new_a_Impl*/Impl) {}
Widget::/*@a_Widget_dtor_def*/~Widget() { delete impl_; }
int Widget::/*@a_Widget_get_def*/get() const { return impl_->/*@use_a_Impl_get*/get(); }
int Widget::/*@a_Widget_size_def*/size() const { return impl_->/*@use_a_Impl_size*/size(); }
/*@use_a_Node_ret*/Node* Widget::/*@a_Widget_node_def*/node() const { return &impl_->node; }
} // namespace a
