#include "util/config.h"
#include "util/types_profile.h"

namespace svc::util {

static int norm(int p0) { return p0 + 8; }

bool Config::splitContract(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::util::kScale1;
    r += static_cast<int>(svc::util::/*@StaticCatalog_0_use7*/StaticCatalog::Default);
    r += norm(1);
    r += svc::util::kScale1;
    r += norm(1);
    return (bool)(r);
}

bool Config::loadAddress(int p0) {
    double r = 0;
    r += p0;
    r += svc::util::kScale1;
    r += norm(1);
    r += norm(1);
    return (bool)(r);
}


}  // namespace svc::util
