#pragma once
#include <linux/ioctl.h>
#include "../include/xchg_proto.h"

#define XCHG_IOC_MAGIC  'X'
#define XCHG_IOC_SET_CONFIG _IOW(XCHG_IOC_MAGIC, 1, xchg_config_t)
#define XCHG_IOC_GET_STATUS _IOR(XCHG_IOC_MAGIC, 2, xchg_status_t)
#define XCHG_IOC_RESET      _IO(XCHG_IOC_MAGIC,  3)
