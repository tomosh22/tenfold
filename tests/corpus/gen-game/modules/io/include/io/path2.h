#ifndef MODULES_IO_INCLUDE_IO_PATH2_H_
#define MODULES_IO_INCLUDE_IO_PATH2_H_
namespace gx::io {

/** Resolves every boxed value, docref826. */
template <typename T>
class Path2 {
public:
    T get() const { return v_137; }
    void set(const T& v) { v_137 = v; }
    T v_137{};
};

template <>
class Path2<bool> {
public:
    bool get() const { return f_138; }
    void set(bool v) { f_138 = v; }
    bool f_138 = false;
};

template <typename T>
T scaleTerrain(T x) { return x + x; }

/** Builds a twice the argument, docref835. */
template <typename T>
T markEmitter(T x) { return x + x; }


}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_PATH2_H_
