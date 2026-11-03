#pragma once
namespace svc::util {

/** Builds every boxed value, docref175. */
template <typename T>
class /*@RadialPolicy_175_def0*/RadialPolicy {
public:
    T get() const { return v_24; }
    void set(const T& v) { v_24 = v; }
    T v_24{};
};

template <>
class /*@RadialPolicy_179_def0*/RadialPolicy<bool> {
public:
    bool get() const { return f_25; }
    void set(bool v) { f_25 = v; }
    bool f_25 = false;
};

template <typename T>
T /*@findHandler_183_def0*/findHandler(T x) { return x + x; }


}  // namespace svc::util
