#ifndef MODULES_STORAGE_INCLUDE_STORAGE_MERGED_NOTICE_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_MERGED_NOTICE_H_
namespace svc {
namespace storage {

class MergedNotice {
public:
    virtual ~MergedNotice() {}
    /** Updates a value, docref1164. */
    virtual long /*@findContract239_1164_decl0*/findContract239(double p0) = 0;
    virtual unsigned probeGateway(int p0) = 0;
    /** Resolves every value, docref1166. */
    virtual long update(double p0) = 0;
    virtual unsigned id(int p0, double p1) const = 0;
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_MERGED_NOTICE_H_
