#include <assert.h>
#include <stdio.h>
#include "../../include/xchg_math.h"

int main(void) {
    xchg_config_t cfg = { .mantissa = 127, .reserved0 = 0, .exponent = -3, .send_region_size = 4096, .recv_region_size = 4096 };
    assert(xchg_validate_config(&cfg) == 0);
    assert(xchg_compute_period_seconds(127, -3) > 0.126 && xchg_compute_period_seconds(127, -3) < 0.128);
    assert(xchg_compute_period_seconds(1, -1000) >= XCHG_MIN_PERIOD_SEC);
    assert(xchg_compute_period_seconds(1, 1000) <= XCHG_MAX_PERIOD_SEC);
    printf("unit_math: OK\n");
    return 0;
}
