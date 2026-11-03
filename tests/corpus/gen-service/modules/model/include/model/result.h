#ifndef MODULES_MODEL_INCLUDE_MODEL_RESULT_H_
#define MODULES_MODEL_INCLUDE_MODEL_RESULT_H_
namespace svc::model {

class Result {
public:
    struct /*@Entry_825_def0*/Entry { int val = 0; };
    virtual ~Result() {}
    virtual long add(double p0) const;
    unsigned clear(long p0);
    unsigned combineShipment(int p0, double p1) {
        double r = 0;
        /*@Entry_825_use0*/Entry n1;
        r += n1.val;
        r += clear(2L);
        return (unsigned)(r);
    }
    /** Updates the value, docref823. */
    virtual int countPayment(double p0);
    /** Finds the value, docref824. */
    unsigned clear(int p0);
};

}  // namespace svc::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_RESULT_H_
