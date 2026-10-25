#include <windows.h>
#include <cstdio>
#include <vector>
#include "../xchg_ioctl_windows.h"

static double now_s() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart/(double)f.QuadPart;
}

int main(int argc, char** argv) {
    int loops = argc > 1 ? std::atoi(argv[1]) : 10000;
    int size = argc > 2 ? std::atoi(argv[2]) : 1024;
    std::vector<char> buf((size_t)size, 'A');
    HANDLE h0 = CreateFileA("\\\\.\\Xchg0", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE h1 = CreateFileA("\\\\.\\Xchg0", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h0 == INVALID_HANDLE_VALUE || h1 == INVALID_HANDLE_VALUE) return 1;
    xchg_config_t cfg = {}; cfg.mantissa = 1; cfg.exponent = -3; cfg.send_region_size = 65536; cfg.recv_region_size = 65536;
    DWORD bytes = 0; if (!DeviceIoControl(h0, IOCTL_XCHG_SET_CONFIG, &cfg, sizeof(cfg), NULL, 0, &bytes, NULL)) return 2;
    double t0 = now_s();
    for (int i=0;i<loops;++i) {
        if (!WriteFile(h0, buf.data(), (DWORD)buf.size(), &bytes, NULL)) return 3;
        if (!ReadFile(h1, buf.data(), (DWORD)buf.size(), &bytes, NULL)) return 4;
    }
    double t1 = now_s();
    std::printf("test_perf: loops=%d size=%d elapsed=%.6f throughput=%.2f MB/s\n", loops, size, t1-t0, ((double)loops*(double)size/(1024.0*1024.0))/(t1-t0));
    CloseHandle(h1); CloseHandle(h0); return 0;
}
