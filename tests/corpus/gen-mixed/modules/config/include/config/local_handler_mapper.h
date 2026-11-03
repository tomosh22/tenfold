#pragma once
#include "config/primary_mapper.h"

namespace mx {
namespace config {

/** Updates every localhandlermapper, docref316. */
class LocalHandlerMapper : public mx::config::PrimaryMapper {
public:
    int flags = 0;
    static int shared_count;
    unsigned select_repository(int p0) const override;
    /** Computes every value, docref318. */
    long /*@reset_318_decl0*/reset(int p0) const;
    long reset(long p0) const;
    /** Updates a value, docref320. */
    int fetch_queue(int p0, int p1) const;
};

}  // namespace config
}  // namespace mx
