#ifndef MODULES_STORAGE_INCLUDE_STORAGE_JOB2_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_JOB2_H_
namespace svc {
namespace storage {

/** Returns the job2, docref1091. */
class Job2 {
public:
    long limit = 0;
    /** Resolves every value, docref1092. */
    double filterValidator(int p0, double p1);
    double apply(double p0, int p1) const;
    long scanPolicy(long p0) const;
    /** Finds a value, docref1095. */
    bool apply(int p0) const {
        double r = 0;
        r += limit;
        r += sweepRoute();
        return (bool)(r);
    }
    bool sweepRoute() const;
    virtual unsigned flushSession(int p0, double p1);
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_JOB2_H_
