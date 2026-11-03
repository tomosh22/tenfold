#ifndef MODULES_CONFIG_INCLUDE_CONFIG_HASHED_SCHEDULE_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_HASHED_SCHEDULE_H_
namespace svc {
namespace config {

/** Resolves a hashedschedule, docref349. */
class HashedSchedule {
public:
    /** Finds every value, docref350. */
    virtual unsigned push(long p0) const = 0;
    /** Builds every value, docref351. */
    virtual long scale_schedule(int p0) = 0;
};

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_HASHED_SCHEDULE_H_
