#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "../../include/xchg_proto.h"
#include "../xchg_ioctl_linux.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
    int loops = argc > 1 ? atoi(argv[1]) : 10000;
    int size  = argc > 2 ? atoi(argv[2]) : 1024;
    char* buf = malloc((size_t)size);
    int fd0 = open("/dev/xchg0", O_RDWR);
    int fd1 = open("/dev/xchg0", O_RDWR);
    xchg_config_t cfg = { .mantissa = 1, .reserved0 = 0, .exponent = -3, .send_region_size = 65536, .recv_region_size = 65536 };
    double t0, t1;
    ssize_t n;
    if (!buf || fd0 < 0 || fd1 < 0) return 1;
    memset(buf, 'A', (size_t)size);
    if (ioctl(fd0, XCHG_IOC_SET_CONFIG, &cfg) < 0) return 2;
    t0 = now_s();
    for (int i = 0; i < loops; ++i) {
        n = write(fd0, buf, (size_t)size);
        if (n < 0) return 3;
        n = read(fd1, buf, (size_t)size);
        if (n < 0) return 4;
    }
    t1 = now_s();
    printf("test_perf: loops=%d size=%d elapsed=%.6f sec throughput=%.2f MB/s\n",
           loops, size, t1 - t0, ((double)loops * (double)size / (1024.0 * 1024.0)) / (t1 - t0));
    close(fd1); close(fd0); free(buf); return 0;
}
