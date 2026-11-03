// Out-of-line members of tb::Stack (included at the end of stack.h).
#pragma once

namespace tb {

template <class T, class A>
int Stack<T, A>::/*@Stack_made_def*/made = 0;

template <class T, class A>
void Stack<T, A>::/*@Stack_push_def*/push(const T& v) { data_.push_back(v); ++/*@use_made_in_push*/made; }

template <class T, class A>
void Stack<T, A>::/*@Stack_push_rv_def*/push(T&& v) { data_.push_back(std::move(v)); }

template <class T, class A>
T& Stack<T, A>::/*@Stack_top_def*/top() { return this->/*@use_data_in_top*/data_.back(); }

template <class T, class A>
const T& Stack<T, A>::/*@Stack_top_c_def*/top() const { return data_.back(); }

template <class T, class A>
Stack<T, A>::/*@Stack_dtor_def*/~Stack() { --live; }

template <class T, class A>
template <class Range>
void Stack<T, A>::/*@Stack_push_all_def*/push_all(const Range& range) {
  for (const auto& e : range) /*@use_push_in_all*/push(e);
}

template <class T, class A>
template <class F>
auto Stack<T, A>::/*@Stack_map_def*/map(F f) const -> Stack<decltype(f(std::declval<T>()))> {
  Stack<decltype(f(std::declval<T>()))> out;
  for (const T& e : data_) out.push(f(e));
  return out;
}

}  // namespace tb
