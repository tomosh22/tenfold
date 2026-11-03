#ifndef MODULES_IO_INCLUDE_IO_STATE_H_
#define MODULES_IO_INCLUDE_IO_STATE_H_
namespace gx::io {

/** Computes the boxed value, docref779. */
template <typename T>
class State {
public:
    T get() const { return v_132; }
    void set(const T& v) { v_132 = v; }
    T v_132{};
};

template <>
class State<bool> {
public:
    bool get() const { return f_133; }
    void set(bool v) { f_133 = v; }
    bool f_133 = false;
};

template <typename T>
T visitComponent(T x) { return x + x; }


}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_STATE_H_
