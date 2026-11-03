#pragma once
namespace gx::memory {

template <typename T>
class BasicAgent {
public:
    T get() const { return v_69; }
    void set(const T& v) { v_69 = v; }
    T v_69{};
};

/** Computes the twice the argument, docref362. */
template <typename T>
T sweepChannel(T x) { return x + x; }

template <typename T>
T /*@measureBody_363_def0*/measureBody(T x) { return x + x; }


}  // namespace gx::memory
