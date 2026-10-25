#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XCHG_PROTO_VERSION 1
#define XCHG_MAX_ENDPOINTS 2
#define XCHG_MIN_REGION_SIZE 4u

/* exponent supports -1000..+1000 */
#define XCHG_MIN_EXPONENT (-1000)
#define XCHG_MAX_EXPONENT (1000)

/* practical timer clamps shared by all implementations */
#define XCHG_MIN_PERIOD_SEC 0.001   /* 1 ms */
#define XCHG_MAX_PERIOD_SEC 86400.0 /* 24 hours */

typedef struct xchg_config {
    uint16_t mantissa;          /* 0..65535, 0 means disabled/fallback */
    int16_t  reserved0;         /* keep struct aligned; ignored */
    int32_t  exponent;          /* -1000..+1000 */
    uint32_t send_region_size;  /* total bytes including 4-byte length header */
    uint32_t recv_region_size;  /* total bytes including 4-byte length header */
} xchg_config_t;

typedef struct xchg_status {
    uint16_t mantissa;
    int16_t  reserved0;
    int32_t  exponent;
    uint32_t send_region_size;
    uint32_t recv_region_size;
    uint32_t tx_payload_size;
    uint32_t rx_payload_size;
    uint64_t epoch_id;
} xchg_status_t;

#ifdef __cplusplus
}
#endif
