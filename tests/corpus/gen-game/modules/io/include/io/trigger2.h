#pragma once
namespace gx::io {

/** Updates the boxed value, docref774. */
template <typename T>
class Trigger2 {
public:
    T get() const { return v_131; }
    void set(const T& v) { v_131 = v; }
    T v_131{};
};

template <typename T>
T /*@resolveSpawner_778_def0*/resolveSpawner(T x) { return x + x; }


}  // namespace gx::io
