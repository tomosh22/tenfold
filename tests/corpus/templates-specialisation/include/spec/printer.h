#pragma once
#include <string>
#include <type_traits>
#include <vector>

namespace spec {

// Primary template, a full specialisation and two partial specialisations.
template <class T>
struct /*@Printer*/Printer {
  static std::string /*@Printer_name*/name() { return "generic"; }
  static int /*@Printer_rank*/rank() { return 0; }
};

template <>
struct /*@Printer_int*/Printer<int> {
  static std::string /*@Printer_int_name*/name() { return "int"; }
  static int /*@Printer_int_extra*/extra() { return 1; }
};

template <class T>
struct /*@Printer_ptr*/Printer<T*> {
  static std::string /*@Printer_ptr_name*/name() { return "pointer"; }
  using /*@Printer_ptr_pointee*/pointee = T;
};

template <class T, class A>
struct /*@Printer_vec*/Printer<std::vector<T, A>> {
  static std::string /*@Printer_vec_name*/name() { return "vector"; }
  static int /*@Printer_vec_rank*/rank() { return 2; }
};

// Variable templates: primary, partial and full specialisation.
template <class T> constexpr int /*@id_v*/id_v = 0;
template <class T> constexpr int /*@id_v_ptr*/id_v<T*> = 1;
template <> constexpr int /*@id_v_int*/id_v<int> = 2;

// Detection idiom with a partial specialisation selected by SFINAE.
template <class T, class = void>
struct /*@has_size*/has_size : std::false_type {};
template <class T>
struct /*@has_size_yes*/has_size<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

}  // namespace spec
