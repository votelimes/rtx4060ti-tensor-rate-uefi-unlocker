# 0.2.9 and instruction benchmark follow-up (2026-11-01)

## Changes and verification

Manual U within five seconds requests an unlock. Timeout or Esc starts Windows
without GPU discovery or writes. The ATTEMPT.LCK mechanism is fully removed:
no read, creation or persistent retry gate. ROM/device/state checks and both logs
remain. A simulation assertion rejects any attempted access to the old lock path.
180 application/core scenarios passed with ASan/UBSan, including normal boot,
manual unlock and loader failures. Obsolete lock-I/O cases were removed.

Built with Zig 0.14.1; EFI size 98,304 bytes, SHA-256:
`b4d0fcf14b4df0be91319a0400b722bd54bed2e46658bdc01575bbd34f91bb1d`.
The copied USB executable was hash-verified. Firmware and raw machine logs are
not distributed. Fresh 0.2.9 UEFI logs have not yet been reviewed; the following
runtime evidence is user-supplied benchmark output, not a log-based boot audit.

## Same-command comparison

Both outputs identify RTX 4060 Ti, 34 SMs, sm_89 and `./mma_bench 50000 10`.
The earlier output was supplied before the user rebooted to run the new loader;
the later output followed the instruction to boot the USB and press U.

| Mode | Earlier best | Later best | Ratio | Unit |
|---|---:|---:|---:|---|
| FP16 / FP16 | 91.2 | 91.7 | 1.01x | TFLOPS |
| FP16 / FP32 | 45.1 | 91.5 | 2.03x | TFLOPS |
| BF16 / FP32 | 45.7 | 91.2 | 2.00x | TFLOPS |
| INT8 / INT32 | 182.0 | 182.5 | 1.00x | TOPS |
| FP8 E4M3 | 91.1 | 182.3 | 2.00x | TFLOPS |
| DP4A | 44.9 | 45.5 | 1.01x | TOPS |
| FFMA | 21.2 | 21.5 | 1.01x | TFLOPS |

All ten samples per mode are in [the data file](mma-2026-11-01.json).
FP16/FP32 catching up with FP16/FP16 is consistent with the intended selector
change. FP16/FP16 was already near 91 TFLOPS and does not double in these results.
Ratios compare reported maxima, not training speed. The first run has substantial
variation in some modes. Power, clocks, temperature, competing workloads and the
benchmark binary hash were not captured with these outputs. No correctness
results were supplied for these runs. Do not interpret them as a controlled
end-to-end PyTorch comparison or proof of hibernation persistence.
