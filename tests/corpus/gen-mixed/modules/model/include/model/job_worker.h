#ifndef MODULES_MODEL_INCLUDE_MODEL_JOB_WORKER_H_
#define MODULES_MODEL_INCLUDE_MODEL_JOB_WORKER_H_
namespace mx::model {

class JobWorker {
public:
    JobWorker() {}
    explicit JobWorker(int) {}
    /** Computes the value, docref1100. */
    double encodeTask252(double p0, int p1) const;
    long encodeTask252(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += encodeTask252(1, 3.5);
        return (long)(r);
    }
};

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_JOB_WORKER_H_
