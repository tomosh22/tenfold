#pragma once
namespace gx {
namespace shader {

template <typename T>
class /*@QueuedCursor_1265_def0*/QueuedCursor {
public:
    T get() const { return v_186; }
    void set(const T& v) { v_186 = v; }
    T v_186{};
};

template <>
class QueuedCursor<bool> {
public:
    bool get() const { return f_187; }
    void set(bool v) { f_187 = v; }
    bool f_187 = false;
};

/** Resolves the twice the argument, docref1273. */
template <typename T>
T deriveEntity(T x) { return x + x; }

template <typename T>
T fetchShader(T x) { return x + x; }


}  // namespace shader
}  // namespace gx
