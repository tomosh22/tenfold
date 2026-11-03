#pragma once
namespace gx::io {

template <typename T>
class Spawner {
public:
    T get() const { return v_128; }
    void set(const T& v) { v_128 = v; }
    T v_128{};
};

template <>
class Spawner<bool> {
public:
    bool get() const { return f_129; }
    void set(bool v) { f_129 = v; }
    bool f_129 = false;
};

/** Computes the twice the argument, docref736. */
template <typename T>
T findEmitter(T x) { return x + x; }


}  // namespace gx::io
