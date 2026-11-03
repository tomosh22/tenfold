// Macros that declare functions, classes, members and enums.
#pragma once
#define /*@DECLARE_CLASS*/DECLARE_CLASS(Name) class Name { public: int /*@Gadget_value*/value; };
#define /*@MAKE_FUNC*/MAKE_FUNC(Name, Expr) inline int Name() { return Expr; }
#define /*@DECLARE_FIELD*/DECLARE_FIELD(type, name) type name##_;
#define /*@DEFINE_GETTER*/DEFINE_GETTER(type, name) type get_##name() const { return name##_; }
#define /*@DECLARE_ENUM*/DECLARE_ENUM(Name, A, B) enum class Name { A, B };
#define /*@PROPERTY*/PROPERTY(type, name) \
  private: type m_##name; \
  public: type name() const { return m_##name; } \
          void set_##name(type v) { m_##name = v; }
// The generated function is named by the caller through an object-like macro.
#define /*@GEN_NAME_ARG*/GEN_NAME_ARG(fn) int fn(int x) { return x + 1; }
