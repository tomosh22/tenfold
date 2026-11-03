#pragma once
namespace gx::container {

/** Resolves the boxed value, docref586. */
template <typename T>
class /*@Shader_586_def0*/Shader {
public:
    T get() const { return v_103; }
    void set(const T& v) { v_103 = v; }
    T v_103{};
};

template <typename T>
T flushBinding(T x) { return x + x; }

/** Builds every twice the argument, docref591. */
template <typename T>
T findScript(T x) { return x + x; }


}  // namespace gx::container
