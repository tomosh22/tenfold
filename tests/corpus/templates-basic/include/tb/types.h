#pragma once
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace tb {

// Dependent names: typename / template disambiguators.
template <class T>
struct /*@Box*/Box {
  using /*@Box_type*/type = T;
  template <class U>
  struct /*@Box_Rebind*/Rebind { using /*@Rebind_type*/type = Box<U>; };
  template <int N>
  int /*@Box_get*/get() const { return N; }
  static constexpr int /*@Box_size*/size = sizeof(T);
  T /*@Box_val*/val;
};

template <class T>
void dependent_names(const Box<T>& b) {
  typename Box<T>::/*@dep_type*/type v = b./*@dep_val*/val;
  typename Box<T>::template /*@dep_rebind*/Rebind<int>::/*@dep_rebind_type*/type r;
  int g = b.template /*@dep_get*/get<3>();
  int sz = Box<T>::/*@dep_size*/size;
  (void)v; (void)r; (void)g; (void)sz;
}

// CTAD and deduction guides.
template <class A, class B>
struct /*@Pair*/Pair {
  A /*@Pair_first*/first;
  B /*@Pair_second*/second;
  /*@Pair_ctor*/Pair(A a, B b) : first(a), second(b) {}
};

template <class T>
struct /*@Wrap*/Wrap {
  T /*@Wrap_v*/v;
  template <class It>
  /*@Wrap_it_ctor*/Wrap(It b, It) : v(*b) {}
};
template <class It>
/*@guide_wrap*/Wrap(It, It) -> Wrap<typename std::iterator_traits<It>::value_type>;

// Alias templates, variable templates, template template parameters.
template <class T> using /*@Vec_t*/Vec_t = std::vector<T>;
template <class T> using /*@Map_str*/Map_str = std::map<std::string, T>;
template <class T> inline constexpr bool /*@is_small_v*/is_small_v = sizeof(T) <= 4;
template <class T> constexpr T /*@pi_v*/pi_v = T(3.14159265358979L);

template <template <class...> class /*@TT*/Container, class T>
struct /*@Holder*/Holder {
  /*@use_TT_in_Holder*/Container<T> /*@Holder_items*/items;
};

}  // namespace tb
