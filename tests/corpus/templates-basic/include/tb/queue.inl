#pragma once
#include "queue.h"

namespace tb {

template <class T>
void Queue<T>::/*@Queue_push_def*/push(T v) { /*@use_items_in_push*/items_.push_back(v); (void)/*@use_empty_in_push*/empty(); }

template <class T>
T Queue<T>::/*@Queue_pop_def*/pop() { T v = this->/*@use_items_in_pop*/items_.front(); /*@use_items_in_pop2*/items_.pop_front(); return v; }

}  // namespace tb
