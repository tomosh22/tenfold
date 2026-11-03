#pragma once
namespace gx::container {

template <typename T>
class Camera {
public:
    T get() const { return v_99; }
    void set(const T& v) { v_99 = v; }
    T v_99{};
};

/** Builds every twice the argument, docref563. */
template <typename T>
T /*@fetchAgent_563_def0*/fetchAgent(T x) { return x + x; }


}  // namespace gx::container
