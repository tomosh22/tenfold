#pragma once
#include <cstddef>
#include <vector>

namespace spec {

template <class T>
class /*@Registry*/Registry {
 public:
  void /*@Registry_add_decl*/add(const T& v);
  int /*@Registry_weight_decl*/weight() const;
  std::size_t /*@Registry_size_decl*/size() const;
  template <class U>
  U /*@Registry_as_decl*/as() const;
 private:
  std::vector<T> items_;
};

template <class T>
void Registry<T>::/*@Registry_add_def*/add(const T& v) { items_.push_back(v); }

template <class T>
int Registry<T>::/*@Registry_weight_def*/weight() const { return static_cast<int>(items_.size()); }

template <class T>
std::size_t Registry<T>::/*@Registry_size_def*/size() const { return items_.size(); }

template <class T>
template <class U>
U Registry<T>::/*@Registry_as_def*/as() const { return static_cast<U>(items_.size()); }

// Member of one instantiation specialised explicitly.
template <>
inline int Registry<int>::/*@Registry_int_weight*/weight() const { return 100; }
template <>
template <>
inline double Registry<int>::/*@Registry_int_as_double*/as<double>() const { return 0.5; }

// The instantiation of Registry<int> is provided by inst.cpp.
extern template class spec::Registry<int>;

}  // namespace spec
