#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <cstring>
#include "../../include/xchg_proto.h"

enum { kXchgMethodSetConfig = 0, kXchgMethodGetStatus = 1, kXchgMethodReadData = 2, kXchgMethodWriteData = 3, kXchgMethodReset = 4 };

int main() {
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XchgDriver"));
    if (!svc) return 1;
    io_connect_t c0 = IO_OBJECT_NULL, c1 = IO_OBJECT_NULL;
    if (IOServiceOpen(svc, mach_task_self(), 0, &c0) != KERN_SUCCESS) return 2;
    if (IOServiceOpen(svc, mach_task_self(), 0, &c1) != KERN_SUCCESS) return 3;
    xchg_config_t cfg = {}; cfg.mantissa = 127; cfg.exponent = -3; cfg.send_region_size = 4096; cfg.recv_region_size = 4096;
    size_t outSize = 0;
    if (IOConnectCallStructMethod(c0, kXchgMethodSetConfig, &cfg, sizeof(cfg), nullptr, &outSize) != KERN_SUCCESS) return 4;
    const char* msg = "hello from macOS";
    if (IOConnectCallStructMethod(c0, kXchgMethodWriteData, msg, std::strlen(msg), nullptr, &outSize) != KERN_SUCCESS) return 5;
    char buf[128] = {};
    outSize = sizeof(buf);
    if (IOConnectCallStructMethod(c1, kXchgMethodReadData, nullptr, 0, buf, &outSize) != KERN_SUCCESS) return 6;
    if (outSize != std::strlen(msg) || std::memcmp(buf, msg, outSize) != 0) return 7;
    std::puts("test_smoke: OK");
    IOServiceClose(c1); IOServiceClose(c0); IOObjectRelease(svc); return 0;
}
