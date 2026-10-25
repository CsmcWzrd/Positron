#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <vector>
#include <chrono>
#include "../../include/xchg_proto.h"

enum { kXchgMethodSetConfig = 0, kXchgMethodGetStatus = 1, kXchgMethodReadData = 2, kXchgMethodWriteData = 3, kXchgMethodReset = 4 };

int main(int argc, char** argv) {
    int loops = argc > 1 ? std::atoi(argv[1]) : 10000;
    int size = argc > 2 ? std::atoi(argv[2]) : 1024;
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XchgDriver"));
    if (!svc) return 1;
    io_connect_t c0 = IO_OBJECT_NULL, c1 = IO_OBJECT_NULL;
    if (IOServiceOpen(svc, mach_task_self(), 0, &c0) != KERN_SUCCESS) return 2;
    if (IOServiceOpen(svc, mach_task_self(), 0, &c1) != KERN_SUCCESS) return 3;
    xchg_config_t cfg = {}; cfg.mantissa = 1; cfg.exponent = -3; cfg.send_region_size = 65536; cfg.recv_region_size = 65536;
    size_t out = 0;
    if (IOConnectCallStructMethod(c0, kXchgMethodSetConfig, &cfg, sizeof(cfg), nullptr, &out) != KERN_SUCCESS) return 4;
    std::vector<char> buf((size_t)size, 'A');
    auto t0 = std::chrono::steady_clock::now();
    for (int i=0;i<loops;++i) {
        if (IOConnectCallStructMethod(c0, kXchgMethodWriteData, buf.data(), buf.size(), nullptr, &out) != KERN_SUCCESS) return 5;
        out = buf.size();
        if (IOConnectCallStructMethod(c1, kXchgMethodReadData, nullptr, 0, buf.data(), &out) != KERN_SUCCESS) return 6;
    }
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1-t0).count();
    std::printf("test_perf: loops=%d size=%d elapsed=%.6f throughput=%.2f MB/s\n", loops, size, sec, ((double)loops*(double)size/(1024.0*1024.0))/sec);
    IOServiceClose(c1); IOServiceClose(c0); IOObjectRelease(svc); return 0;
}
