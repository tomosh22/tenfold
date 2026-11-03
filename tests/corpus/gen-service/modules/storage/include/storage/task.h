#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TASK_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TASK_H_
namespace svc {
namespace storage {

/** Finds the task, docref1016. */
class Task {
public:
    /** Finds every value, docref1017. */
    virtual double visitCatalog(int p0, double p1) const = 0;
    virtual double push(unsigned p0) = 0;
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TASK_H_
