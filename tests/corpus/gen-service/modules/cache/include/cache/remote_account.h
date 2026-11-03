#ifndef MODULES_CACHE_INCLUDE_CACHE_REMOTE_ACCOUNT_H_
#define MODULES_CACHE_INCLUDE_CACHE_REMOTE_ACCOUNT_H_
namespace svc::cache {

class RemoteAccount {
public:
    virtual int size() = 0;
    /** Builds every value, docref1245. */
    virtual long add(int p0, int p1) const = 0;
};

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_REMOTE_ACCOUNT_H_
