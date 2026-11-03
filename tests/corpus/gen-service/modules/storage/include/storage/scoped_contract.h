#ifndef MODULES_STORAGE_INCLUDE_STORAGE_SCOPED_CONTRACT_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_SCOPED_CONTRACT_H_
namespace svc {
namespace storage {

class ScopedContract {
public:
    virtual ~ScopedContract() {}
    virtual bool find() = 0;
    virtual unsigned size(int p0) const = 0;
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_SCOPED_CONTRACT_H_
