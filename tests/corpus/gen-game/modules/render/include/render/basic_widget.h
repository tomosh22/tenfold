#pragma once
namespace gx {
namespace render {

template <typename T>
class BasicWidget {
public:
    T get() const { return v_167; }
    void set(const T& v) { v_167 = v; }
    T v_167{};
};

template <>
class BasicWidget<bool> {
public:
    bool get() const { return f_168; }
    void set(bool v) { f_168 = v; }
    bool f_168 = false;
};

/** Resolves every twice the argument, docref1121. */
template <typename T>
T resetSkeleton(T x) { return x + x; }

/** Resolves the twice the argument, docref1122. */
template <typename T>
T scaleCurve(T x) { return x + x; }


}  // namespace render
}  // namespace gx
