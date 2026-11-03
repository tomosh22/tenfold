#pragma once
namespace gx {
namespace shader {

/** Finds every boxed value, docref1251. */
template <typename T>
class Sprite {
public:
    T get() const { return v_184; }
    void set(const T& v) { v_184 = v; }
    T v_184{};
};

template <>
class Sprite<bool> {
public:
    bool get() const { return f_185; }
    void set(bool v) { f_185 = v; }
    bool f_185 = false;
};

template <typename T>
T countQuat(T x) { return x + x; }

/** Resolves the twice the argument, docref1260. */
template <typename T>
T selectBuffer(T x) { return x + x; }


}  // namespace shader
}  // namespace gx
