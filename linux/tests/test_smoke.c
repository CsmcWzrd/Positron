#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../include/xchg_proto.h"
#include "../xchg_ioctl_linux.h"

static void die(const char* msg) { perror(msg); exit(1); }

int main(void) {
    int fd0 = open("/dev/xchg0", O_RDWR);
    if (fd0 < 0) die("open fd0");
    int fd1 = open("/dev/xchg0", O_RDWR);
    if (fd1 < 0) die("open fd1");

    xchg_config_t cfg = { .mantissa = 127, .reserved0 = 0, .exponent = -3, .send_region_size = 4096, .recv_region_size = 4096 };
    if (ioctl(fd0, XCHG_IOC_SET_CONFIG, &cfg) < 0) die("ioctl setcfg");

    const char *msg = "hello from endpoint0";
    char buf[128] = {0};
    ssize_t n;

    n = write(fd0, msg, strlen(msg));
    if (n < 0) die("write");
    n = read(fd1, buf, sizeof(buf));
    if (n < 0) die("read");
    if ((size_t)n != strlen(msg) || memcmp(buf, msg, (size_t)n) != 0) {
        fprintf(stderr, "mismatch\n");
        return 2;
    }

    n = read(fd1, buf, sizeof(buf));
    if (n != 0) {
        fprintf(stderr, "expected EOF-like 0 after full consume, got %zd\n", n);
        return 3;
    }

    close(fd1);
    close(fd0);
    printf("test_smoke: OK\n");
    return 0;
}
