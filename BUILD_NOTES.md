# Build notes

## Linux

```bash
cd linux
make
sudo insmod xchg_linux.ko
make user-tests
./unit_math
./test_smoke
./test_perf 10000 4096
sudo rmmod xchg_linux
```

## Windows

- Open in Visual Studio with WDK installed.
- Create a KMDF driver project and add:
  - `xchg_windows.c`
  - `xchg_ioctl_windows.h`
  - shared headers from `include/`
- Build `xchg.sys`, sign for test mode, install with `pnputil /add-driver xchg.inf /install`
- Build tests under a Console App project and run:
  - `unit_math.exe`
  - `test_smoke.exe`
  - `test_perf.exe 10000 4096`

## macOS

- Create a DriverKit extension target and add:
  - `XchgDriver.*`
  - `XchgUserClient.*`
  - shared headers from `include/`
- Add the proper entitlements and DriverKit Info.plist entries for your team ID.
- Build and install the system extension.
- Build test binaries and run:
  - `unit_math`
  - `test_smoke`
  - `test_perf 10000 4096`
