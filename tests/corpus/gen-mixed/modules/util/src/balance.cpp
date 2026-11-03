#include "util/balance.h"
#include "util/funcs_repository.h"
#include "util/token.h"

namespace mx {
namespace util {

static int step(int p0) { return p0 + 2; }

bool Balance::count(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += mx::util::flushEndpoint(2L);
    r += mx::util::flushAccount(1);
    mx::util::Token o1;
    r += o1./*@rotateAudit_171_use1*/rotateAudit(1);
    r += mx::util::Token::value();
    return (bool)(r);
}


}  // namespace util
}  // namespace mx
