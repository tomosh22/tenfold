#pragma once
namespace mx::model {

class /*@Job_1081_def0*/Job {
public:
    unsigned total = 0;
    double score = 0;
    bool verifyTariff() const;
    /** Resolves every value, docref1083. */
    long verifyTariff(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += total;
        r += this->score;
        return (long)(r);
    }
    /** Builds the value, docref1084. */
    double size();
};

}  // namespace mx::model
