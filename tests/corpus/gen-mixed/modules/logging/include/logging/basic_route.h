#ifndef MODULES_LOGGING_INCLUDE_LOGGING_BASIC_ROUTE_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_BASIC_ROUTE_H_
namespace mx::logging {

/** Returns every basicroute, docref578. */
class BasicRoute {
public:
    long total = 0;
    double index = 0;
    double phase = 0;
    virtual unsigned run() {
        double r = 0;
        r += phase;
        r += next(3.5, 1);
        return (unsigned)(r);
    }
    virtual long next(double p0, int p1) const {
        double r = 0;
        r += p1;
        r += process();
        r += this->total;
        return (long)(r);
    }
    /** Builds a value, docref581. */
    static bool combine_ticket(int p0);
    /** Finds every value, docref582. */
    static double /*@save_582_def0*/save(int p0, int p1) {
        double r = 0;
        r += p1;
        r += combine_ticket(1);
        return (double)(r);
    }
    /** Finds the value, docref583. */
    unsigned process() const;
};

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_BASIC_ROUTE_H_
