#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../xchg_ioctl_windows.h"

static int fail(const char* msg) { std::fprintf(stderr, "%s: %lu\n", msg, GetLastError()); return 1; }

int main() {
    HANDLE h0 = CreateFileA("\\\\.\\Xchg0", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h0 == INVALID_HANDLE_VALUE) return fail("CreateFile h0");
    HANDLE h1 = CreateFileA("\\\\.\\Xchg0", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h1 == INVALID_HANDLE_VALUE) return fail("CreateFile h1");

    xchg_config_t cfg = {}; cfg.mantissa = 127; cfg.exponent = -3; cfg.send_region_size = 4096; cfg.recv_region_size = 4096;
    DWORD bytes = 0;
    if (!DeviceIoControl(h0, IOCTL_XCHG_SET_CONFIG, &cfg, sizeof(cfg), NULL, 0, &bytes, NULL)) return fail("SET_CONFIG");

    const char* msg = "hello from windows";
    char buf[128] = {};
    if (!WriteFile(h0, msg, (DWORD)std::strlen(msg), &bytes, NULL)) return fail("WriteFile");
    if (!ReadFile(h1, buf, sizeof(buf), &bytes, NULL)) return fail("ReadFile");
    if (bytes != std::strlen(msg) || std::memcmp(buf, msg, bytes) != 0) return 2;
    std::puts("test_smoke: OK");
    CloseHandle(h1); CloseHandle(h0); return 0;
}
