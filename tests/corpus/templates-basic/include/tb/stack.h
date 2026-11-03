#pragma once
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace tb {

// Class template whose members are defined out of line in stack.tpp.
template <class T, class Alloc = std::allocator<T>>
class /*@Stack*/Stack {
 public:
  using /*@Stack_value_type*/value_type = T;
  using /*@Stack_size_type*/size_type = std::size_t;

  struct /*@Stack_Node*/Node {
    T /*@Node_value*/value;
  };

  void /*@Stack_push_decl*/push(const T& v);
  void /*@Stack_push_rv_decl*/push(T&& v);
  T& /*@Stack_top_decl*/top();
  const T& /*@Stack_top_c_decl*/top() const;
  /*@Stack_ctor_decl*/Stack() = default;
  /*@Stack_dtor_decl*/~Stack();

  template <class Range>
  void /*@Stack_push_all_decl*/push_all(const Range& range);

  template <class F>
  auto /*@Stack_map_decl*/map(F f) const -> Stack<decltype(f(std::declval<T>()))>;

  size_type /*@Stack_size*/size() const { return /*@use_data_in_size*/data_.size(); }
  Node /*@Stack_first_node*/first_node() const { return Node{data_.front()}; }

  static inline int /*@Stack_live*/live = 0;
  static int /*@Stack_made*/made;

 private:
  std::vector<T, Alloc> /*@Stack_data*/data_;
};

}  // namespace tb

#include "stack.tpp"
