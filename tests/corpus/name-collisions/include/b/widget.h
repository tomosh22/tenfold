#pragma once
#include "b/fwd.h"
namespace b {
class /*@b_Widget*/Widget {
 public:
  /*@b_Widget_ctor*/Widget();
  /*@b_Widget_dtor*/~Widget();
  int /*@b_Widget_get*/get() const;
  int /*@b_Widget_size*/size() const;
  struct /*@b_Impl_decl*/Impl;                // nested pimpl, not b::Impl
 private:
  /*@use_b_Impl_in_Widget*/Impl* /*@b_Widget_impl*/impl_;
};
} // namespace b
