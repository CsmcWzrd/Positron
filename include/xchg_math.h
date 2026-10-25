#pragma once
#include <math.h>
#include <stdint.h>
#include "xchg_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int xchg_validate_config(const xchg_config_t* cfg) {
    if (!cfg) return -1;
    if (cfg->exponent < XCHG_MIN_EXPONENT || cfg->exponent > XCHG_MAX_EXPONENT) return -2;
    if (cfg->send_region_size < XCHG_MIN_REGION_SIZE || cfg->recv_region_size < XCHG_MIN_REGION_SIZE) return -3;
    return 0;
}

static inline double xchg_compute_period_seconds(uint16_t mantissa, int exponent) {
    double log10_t;
    if (mantissa == 0) return 1.0;
    if (exponent < XCHG_MIN_EXPONENT) exponent = XCHG_MIN_EXPONENT;
    if (exponent > XCHG_MAX_EXPONENT) exponent = XCHG_MAX_EXPONENT;
    log10_t = log10((double)mantissa) + (double)exponent;
    if (log10_t < log10(XCHG_MIN_PERIOD_SEC)) return XCHG_MIN_PERIOD_SEC;
    if (log10_t > log10(XCHG_MAX_PERIOD_SEC)) return XCHG_MAX_PERIOD_SEC;
    return pow(10.0, log10_t);
}

#ifdef __cplusplus
}
#endif
