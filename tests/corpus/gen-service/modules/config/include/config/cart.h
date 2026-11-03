#pragma once
namespace svc {
namespace config {

template <typename T>
class /*@Cart_205_def0*/Cart {
public:
    T get() const { return v_38; }
    void set(const T& v) { v_38 = v; }
    T v_38{};
};

template <>
class /*@Cart_209_def0*/Cart<bool> {
public:
    bool get() const { return f_39; }
    void set(bool v) { f_39 = v; }
    bool f_39 = false;
};

/** Builds a twice the argument, docref213. */
template <typename T>
T collect_token(T x) { return x + x; }


}  // namespace config
}  // namespace svc
