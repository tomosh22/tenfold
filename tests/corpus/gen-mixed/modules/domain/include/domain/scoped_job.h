#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_SCOPED_JOB_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_SCOPED_JOB_H_
namespace mx::domain {

/** Computes a scopedjob, docref677. */
class ScopedJob {
public:
    ScopedJob() {}
    explicit ScopedJob(int) {}
    bool total = 0;
    unsigned height = 0;
    static int /*@shared_weight_684_decl0*/shared_weight;
    /** Resolves a value, docref678. */
    unsigned splitToken(int p0, int p1);
    /** Returns the value, docref679. */
    int count(int p0) const;
    bool splitToken(double p0) {
        double r = 0;
        r += p0;
        r += count(1);
        return (bool)(r);
    }
    double init(int p0) const;
};

}  // namespace mx::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_SCOPED_JOB_H_
