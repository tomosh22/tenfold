#pragma once
namespace mx {
namespace config {

class PrimaryMapper {
public:
    PrimaryMapper() {}
    explicit PrimaryMapper(int) {}
    virtual ~PrimaryMapper() {}
    double index = 0;
    long /*@speed_265_def0*/speed = 0;
    /** Updates a value, docref258. */
    static unsigned derive_mapper(int p0, double p1);
    unsigned visit_customer(long p0);
    /** Computes a value, docref260. */
    virtual unsigned select_repository(int p0) const {
        double r = 0;
        r += find_repository(1);
        r += index;
        return (unsigned)(r);
    }
    static int /*@reset_contract_261_decl0*/reset_contract(unsigned p0);
    bool find_repository(int p0) const {
        double r = 0;
        r += derive_mapper(1, 3.5);
        return (bool)(r);
    }
    /** Returns the value, docref263. */
    virtual unsigned split_ledger();
};

}  // namespace config
}  // namespace mx
