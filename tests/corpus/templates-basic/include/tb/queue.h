#pragma once
#include <deque>

namespace tb {

// Like Stack, but queue.inl includes this header itself, so it also parses on its own.
template <class T>
class /*@Queue*/Queue {
 public:
  void /*@Queue_push_decl*/push(T v);
  T /*@Queue_pop_decl*/pop();
  bool /*@Queue_empty*/empty() const { return /*@Queue_items_use*/items_.empty(); }

 private:
  std::deque<T> /*@Queue_items*/items_;
};

}  // namespace tb

#include "queue.inl"
