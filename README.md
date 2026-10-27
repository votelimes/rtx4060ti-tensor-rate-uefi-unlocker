# RTX 4060 Ti UEFI unlocker

Experimental, board-specific UEFI application for changing Ada tensor throughput
selectors before loading Windows. Derived from the projects credited in [NOTICE.md](NOTICE.md).

**This is not a universal RTX 4060 Ti unlocker.** The profile targets one tested
Palit RTX 4060 Ti JetStream 16 GB and its captured ROM. Other boards, the 8 GB
variant, different ROMs and different PCI resource layouts are deliberately rejected.
It does not enable physically absent cores or increase VRAM.

## Status

- **0.2.5:** one successful physical unlock boot, GOP recovery and Windows handoff.
  Selectors and effective readout matched after recovery. CUDA/PyTorch workloads ran.
- **0.2.6 (this source):** automatic start after a five-second cancellation window;
  179 application simulation scenarios passed with ASan/UBSan. This automatic
  entry point has **not** been tested on the physical machine.
- The unchanged transaction core previously passed 45 simulation scenarios.
  Five journal-decoder unit tests are also available.
- No long-term stability, repeat-boot reliability or persistence guarantee.

On the tested AMI X99 platform, the legacy BIOS video driver hung during post-reset
reconnection. Switching **Video OpROM policy from Legacy to UEFI** selected the
native NVIDIA GPU UEFI driver, after which the same 0.2.5 executable completed.
This is an observation on one board, not a universal fix.

## Compatibility gates

| Property | Required profile |
|---|---|
| PCI vendor/device | `10de:2803` |
| Subsystem | `1569:f314` |
| Chip register | `196000a1` (AD106) |
| Memory register | `0x3ffc`, 16,380 MiB reported on the tested 16 GB card |
| PCI address | `0000:03:00.0` |
| BAR0 | `0xfa000000` |
| ROM | Exact profile hashes; see [profile.json](profile.json) |

PCI ID alone does not establish the VRAM capacity. The application checks the
memory register as well as chip, subsystem, ROM and state. Checks run before the
countdown and again before/after releasing the firmware driver. Do not loosen
these gates merely to make another card pass.

## Build

Requirements: Python 3 and **Zig 0.14.1**. Required EDK2 headers are included with
their notices. No proprietary firmware is needed to compile the EFI application.

```sh
python3 build.py --zig /path/to/zig
```

Output: `USB/EFI/BOOT/BOOTX64.EFI` and `build-result.json` with checksums.
There is no prebuilt firmware bundle in this repository.

Execution additionally requires all five exact files listed in
[ASSETS-MANIFEST.json](ASSETS-MANIFEST.json). Obtain them separately, following the
upstream project's firmware preparation instructions and applicable licenses.
They use the pinned 610.43.03 assets; arbitrary newer files are not substitutes.
Verify and stage your local copies:

```sh
python3 prepare_assets.py /path/to/your/local/assets
```

The script verifies every file before copying any, then stages them under
`USB/AD106-EXPERIMENT/ASSETS`. It does not download or extract firmware.

## One experimental boot

Read [Safety and recovery](docs/SAFETY.md) first. Back up the USB's existing EFI
loader and logs. Copy the contents of `USB/` onto a FAT32 USB boot partition.
Do not format a disk or overwrite an internal EFI system partition for this test.

1. Keep the firmware's video option-ROM policy on **UEFI**. Secure Boot must be
   off for this unsigned experimental application. The application refuses an
   enabled or unreadable Secure Boot state; it does not change that setting.
2. Shut the PC down and remove mains power for ten seconds before a fresh attempt.
3. Select the USB's **UEFI** entry in the one-time boot menu.
4. Preflight checks run and Windows is preloaded. After `AUTO START`, a five-second
   countdown begins. **No U confirmation is needed. Esc cancels.** Keyboard-read
   errors stop the attempt; pressing U does not skip the countdown.
5. A durable `ATTEMPT.LCK` is created before GPU driver release. If all transaction
   and recovery checks pass, control transfers to Windows.
6. Keep that Windows session running for the after-unlock benchmark.

**Automatic start is not automatic retry.** The lock blocks later attempts,
including after a successful boot. Review and save the logs, power off, and only
then deliberately archive the lock before another authorized experiment. The
program never clears it itself. BootOrder/BootNext and VBIOS are not changed.

## Logs

Each run creates a new `AD106-EXPERIMENT/RUNxx` directory on the boot USB.
`REPORT.TXT` is flushed after writes. `SAFETY.LOG` alternates two preallocated,
checksummed slots with write/flush/readback verification.

```sh
python3 read_safety_log.py /path/to/USB/AD106-EXPERIMENT/RUN07/SAFETY.LOG
```

`POST-GOP UNLOCK_MATCHED=1` plus a clean final handoff establishes the observed
pre-OS register state. It does not by itself establish application speed or the
state after the Windows driver loads. Measure both correctness and performance.

## Results on the tested board

Windows NVIDIA driver 617.14, WSL2 Ubuntu, PyTorch 2.14.1+cu130, unchanged **100 W**
power limit. Same PyTorch script and configuration before/after; all 12 correctness
checks passed. One full run per state, seven timing repetitions per case.

| Workload | Before | After | Speedup |
|---|---:|---:|---:|
| Transformer training FP16, steps/s | 51.95 | 58.10 | 1.118x |
| Transformer training BF16, steps/s | 55.36 | 61.43 | 1.110x |
| GEMM 8192 FP16, TFLOPS | 38.72 | 43.74 | 1.129x |
| GEMM 8192 BF16, TFLOPS | 39.01 | 46.74 | 1.198x |

A separate upstream instruction probe measured 83.18 TFLOPS FP16/FP32,
44.60 TFLOPS TF32/FP32 and 358.35 TOPS INT4/INT32 after unlock at 100 W,
with zero mismatches. **There is no local stock baseline for that probe**, so
these numbers do not establish a local 2x instruction speedup.

See [benchmark methodology](docs/BENCHMARKS.md) and
[machine-readable measurements](docs/measurements.json). These results do not
predict a universal 2x training improvement or a result at a different power cap.

## Tests

Journal reader (Python only):

```sh
python3 test_safety_reader.py
```

Transaction/application simulation requires Linux, GCC with ASan/UBSan, staged
assets, and the original matching board ROM and PROM capture (not distributed):

```sh
python3 test.py --group all --rom /private/AD106.rom --prom /private/PROM.BIN
```

The test harness verifies the input hashes before use. PCI/MMIO/DMA, firmware
execution and Windows handoff are simulated; passing tests cannot prove hardware
safety. Source and dependency license information is in [NOTICE.md](NOTICE.md).
