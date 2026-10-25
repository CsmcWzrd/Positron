# XCHG timed data-exchange driver package
### Name has been since changed to positron driver

This package contains a **full Linux implementation** and **production-grade reference implementations** for **Windows KMDF** and **macOS DriverKit** with the same protocol and behavior.

The timing model is:

```text
one frame per (mantissa × 10^exponent) seconds
```

Where:
- `mantissa`: unsigned 16-bit (`0..65535`)
- `exponent`: signed integer (`-1000..+1000`)

Because extreme exponents cannot be represented directly, all platforms use logarithmic scaling and clamp the practical timer interval into a sane OS-supported range.

## Status

- **Linux**: complete buildable kernel char-device driver with tests.
- **Windows**: KMDF driver source, INF, user-mode test suite, and VS build notes. This is production-grade reference code but has **not** been compiled in this environment.
- **macOS**: DriverKit service + user client source and user-space tests. This is production-grade reference code but has **not** been compiled in this environment.

## Device behavior

- Exactly **two processes** may open the device at once.
- First opener becomes endpoint 0, second opener becomes endpoint 1.
- Endpoint 0 writes to channel `0→1`; endpoint 1 reads it.
- Endpoint 1 writes to channel `1→0`; endpoint 0 reads it.
- Each region begins with a 32-bit payload length.
- Reads within an epoch see the same frame until fully consumed.
- After full consumption, further reads in the same epoch return 0 until new data is written or the epoch advances.
- Writes replace the current frame for the channel and reset the peer read cursor.

## Layout

- `include/` shared ABI and math helpers
- `linux/` Linux kernel driver, Makefile, user-space tests
- `windows/` KMDF driver, INF, user-space tests
- `macos/` DriverKit source, plist template, user-space tests

## Notes

- Linux is the only target that can be realistically built and exercised from this environment.
- Windows and macOS code is complete source meant for real driver projects, but you will need WDK/Xcode/entitlements on the host platform.
