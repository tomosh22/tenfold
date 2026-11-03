#pragma once
namespace gx {
namespace shader {

/** Builds the boxed value, docref1223. */
template <typename T>
class Options {
public:
    T get() const { return v_180; }
    void set(const T& v) { v_180 = v; }
    T v_180{};
};

/** Computes the twice the argument, docref1227. */
template <typename T>
T verifyEmitter(T x) { return x + x; }

/** Builds a twice the argument, docref1228. */
template <typename T>
T encodeCursor(T x) { return x + x; }


}  // namespace shader
}  // namespace gx
