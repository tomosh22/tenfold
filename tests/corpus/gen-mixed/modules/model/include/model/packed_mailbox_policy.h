#pragma once
namespace mx::model {

/** Resolves a boxed value, docref1050. */
template <typename T>
class /*@PackedMailboxPolicy_1050_def0*/PackedMailboxPolicy {
public:
    T get() const { return v_242; }
    void set(const T& v) { v_242 = v; }
    T v_242{};
};

/** Builds the twice the argument, docref1054. */
template <typename T>
T /*@markPayment_1054_def0*/markPayment(T x) { return x + x; }

/** Returns a twice the argument, docref1055. */
template <typename T>
T /*@encodeSession_1055_def0*/encodeSession(T x) { return x + x; }


}  // namespace mx::model
