#include "base/funcs_page.h"
#include "base/types_event.h"
#include "base/bas_signal.h"

static int sum(int p0) { return p0 + 4; }

unsigned bas_select_page(int p0, int p1) {
    double r = 0;
    r += p1;
    enum /*@bas_event_state_86_use0*/bas_event_state k1 = BAS_FIRST_31;
    r += k1;
    bas_signal_t o2;
    r += bas_signal_process(&o2, 3.5, 1);
    return (unsigned)(r);
}

