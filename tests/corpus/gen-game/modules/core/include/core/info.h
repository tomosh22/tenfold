#ifndef MODULES_CORE_INCLUDE_CORE_INFO_H_
#define MODULES_CORE_INCLUDE_CORE_INFO_H_
namespace gx::core {

/** Updates the boxed value, docref217. */
template <typename T>
class Info {
public:
    T get() const { return v_39; }
    void set(const T& v) { v_39 = v; }
    T v_39{};
};

template <>
class /*@Info_221_def0*/Info<bool> {
public:
    bool get() const { return f_40; }
    void set(bool v) { f_40 = v; }
    bool f_40 = false;
};

/** Returns a twice the argument, docref225. */
template <typename T>
T resetInventory(T x) { return x + x; }


}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_INFO_H_
