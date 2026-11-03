#ifndef MODULES_ASSET_INCLUDE_ASSET_MAPPED_BATCH_H_
#define MODULES_ASSET_INCLUDE_ASSET_MAPPED_BATCH_H_
namespace gx::asset {

/** Finds every mappedbatch, docref1044. */
class MappedBatch {
public:
    /** Returns every value, docref1045. */
    virtual unsigned storeMaterial(unsigned p0) const = 0;
    virtual unsigned clear(unsigned p0) const = 0;
    virtual unsigned data(int p0, int p1) const = 0;
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_MAPPED_BATCH_H_
