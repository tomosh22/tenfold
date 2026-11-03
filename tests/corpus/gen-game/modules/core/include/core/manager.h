#ifndef MODULES_CORE_INCLUDE_CORE_MANAGER_H_
#define MODULES_CORE_INCLUDE_CORE_MANAGER_H_
namespace gx::core {

/** Builds a manager, docref302. */
class Manager {
public:
    virtual bool encodeQuest(int p0, double p1) const = 0;
    virtual int encodeClip(int p0, int p1) const = 0;
    /** Computes the value, docref305. */
    virtual bool resolveMaterial(unsigned p0) = 0;
    /** Finds the value, docref306. */
    virtual int /*@add_306_decl0*/add(double p0) = 0;
};

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_MANAGER_H_
