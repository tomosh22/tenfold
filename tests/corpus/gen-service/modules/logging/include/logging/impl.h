#ifndef MODULES_LOGGING_INCLUDE_LOGGING_IMPL_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_IMPL_H_
namespace svc {
namespace logging {

class Impl {
public:
    int flags = 0;
    double level = 0;
    static int /*@shared_total_477_decl0*/shared_total;
    /** Returns every value, docref471. */
    unsigned flush(unsigned p0) const {
        double r = 0;
        r += flags;
        r += this->flags;
        return (unsigned)(r);
    }
    bool fetch_rule(int p0, int p1) const;
    unsigned fetch_rule(double p0) const;
    /** Updates a value, docref474. */
    double init(long p0) const;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_IMPL_H_
