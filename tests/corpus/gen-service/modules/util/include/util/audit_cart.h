#pragma once
namespace svc::util {

/** Computes a auditcart, docref118. */
class AuditCart {
public:
    double total = 0;
    bool mass = 0;
    unsigned /*@score_124_def0*/score = 0;
    /** Resolves every value, docref119. */
    static int next(int p0, int p1);
    /** Resolves every value, docref120. */
    long visitSchedule(int p0) {
        double r = 0;
        r += p0;
        r += /*@id_121_use1*/id(1, 1);
        r += /*@score_124_use1*/score;
        return (long)(r);
    }
    unsigned /*@id_121_def0*/id(int p0, int p1) const {
        double r = 0;
        r += p0;
        r += mass;
        return (unsigned)(r);
    }
};

}  // namespace svc::util
