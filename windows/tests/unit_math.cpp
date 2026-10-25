#include <cassert>
#include <cstdio>
extern "C" {
#include "../../include/xchg_math.h"
}
int main() {
    assert(xchg_compute_period_seconds(127, -3) > 0.126 && xchg_compute_period_seconds(127, -3) < 0.128);
    assert(xchg_compute_period_seconds(1, -1000) >= XCHG_MIN_PERIOD_SEC);
    assert(xchg_compute_period_seconds(1, 1000) <= XCHG_MAX_PERIOD_SEC);
    std::puts("unit_math: OK");
    return 0;
}
