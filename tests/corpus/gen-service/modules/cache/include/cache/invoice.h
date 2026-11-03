#pragma once
#include "cache/config.h"

namespace svc::cache {

class Invoice : public svc::cache::Config {
public:
    long visitWorker(long p0);
    bool rotateBalance(long p0) const;
};

}  // namespace svc::cache
