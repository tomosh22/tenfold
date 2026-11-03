#pragma once
namespace gx::core {

/** Returns the boxed value, docref268. */
template <typename T>
class DynamicGlyphFrame {
public:
    T get() const { return v_56; }
    void set(const T& v) { v_56 = v; }
    T v_56{};
};

template <>
class DynamicGlyphFrame<bool> {
public:
    bool get() const { return f_57; }
    void set(bool v) { f_57 = v; }
    bool f_57 = false;
};

/** Builds every twice the argument, docref276. */
template <typename T>
T findTween(T x) { return x + x; }


}  // namespace gx::core
