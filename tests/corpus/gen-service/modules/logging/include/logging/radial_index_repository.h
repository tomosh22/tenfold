#pragma once
namespace svc {
namespace logging {

/** Builds every radialindexrepository, docref454. */
class RadialIndexRepository {
public:
    RadialIndexRepository() {}
    explicit RadialIndexRepository(int) {}
    long limit = 0;
    int level = 0;
    bool mass = 0;
    static int shared_height;
    double process(int p0, double p1) {
        double r = 0;
        r += find_repository(1, 1);
        return (double)(r);
    }
    /** Builds every value, docref456. */
    long find_repository(int p0, int p1) const;
};

}  // namespace logging
}  // namespace svc
