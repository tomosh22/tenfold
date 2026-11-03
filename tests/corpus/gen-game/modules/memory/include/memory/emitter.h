#pragma once
namespace gx::memory {

/** Computes every boxed value, docref348. */
template <typename T>
class Emitter {
public:
    T get() const { return v_68; }
    void set(const T& v) { v_68 = v; }
    T v_68{};
};

template <typename T>
T resetBatch(T x) { return x + x; }


}  // namespace gx::memory
