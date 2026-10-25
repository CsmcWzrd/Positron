obj-m += xchg_linux.o
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) test_smoke test_perf test_math unit_math

user-tests: test_smoke test_perf unit_math

test_smoke: tests/test_smoke.c ../include/xchg_math.h ../include/xchg_proto.h xchg_ioctl_linux.h
	$(CC) -O2 -Wall -Wextra -I../include -I. $< -lm -o $@

test_perf: tests/test_perf.c ../include/xchg_math.h ../include/xchg_proto.h xchg_ioctl_linux.h
	$(CC) -O2 -Wall -Wextra -I../include -I. $< -lm -o $@

unit_math: tests/unit_math.c ../include/xchg_math.h ../include/xchg_proto.h
	$(CC) -O2 -Wall -Wextra -I../include $< -lm -o $@
