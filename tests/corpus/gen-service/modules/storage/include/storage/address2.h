#ifndef MODULES_STORAGE_INCLUDE_STORAGE_ADDRESS2_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_ADDRESS2_H_
namespace svc {
namespace storage {

/** Returns every boxed value, docref1019. */
template <typename T>
class Address2 {
public:
    T get() const { return v_208; }
    void set(const T& v) { v_208 = v; }
    T v_208{};
};

template <>
class /*@Address2_1023_def0*/Address2<bool> {
public:
    bool get() const { return f_209; }
    void set(bool v) { f_209 = v; }
    bool f_209 = false;
};

template <typename T>
T /*@decodeSession_1027_def0*/decodeSession(T x) { return x + x; }

template <typename T>
T mergeNotice(T x) { return x + x; }


}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_ADDRESS2_H_
