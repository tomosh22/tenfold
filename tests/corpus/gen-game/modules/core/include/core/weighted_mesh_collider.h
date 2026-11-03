#pragma once
namespace gx::core {

template <typename T>
class WeightedMeshCollider {
public:
    T get() const { return v_33; }
    void set(const T& v) { v_33 = v; }
    T v_33{};
};

template <>
class /*@WeightedMeshCollider_181_def0*/WeightedMeshCollider<bool> {
public:
    bool get() const { return f_34; }
    void set(bool v) { f_34 = v; }
    bool f_34 = false;
};

template <typename T>
T selectShader(T x) { return x + x; }

template <typename T>
T fetchScript(T x) { return x + x; }


}  // namespace gx::core
