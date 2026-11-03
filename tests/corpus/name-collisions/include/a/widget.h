#pragma once
#include "a/fwd.h"
namespace a {
class /*@a_Widget*/Widget {
 public:
  /*@a_Widget_ctor*/Widget();
  /*@a_Widget_dtor*/~Widget();
  int /*@a_Widget_get*/get() const;
  int /*@a_Widget_size*/size() const;
  /*@use_a_Node_in_Widget*/Node* /*@a_Widget_node*/node() const;   // incomplete type here
 private:
  /*@use_a_Impl_in_Widget*/Impl* /*@a_Widget_impl*/impl_;           // incomplete type here
};
} // namespace a
