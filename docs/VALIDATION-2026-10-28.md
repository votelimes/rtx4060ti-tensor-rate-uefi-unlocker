# Hardware validation: 2026-10-28

## Automatic entry point 0.2.6

RUN08 on the same Palit RTX 4060 Ti JetStream 16 GB completed successfully.
The USB report and both safety-journal slots were inspected locally. The user
confirmed that Windows booted and PyTorch computations were noticeably faster.
No additional benchmarks were run during this log review.

- Executable SHA-256: `cdb0ba9af2f08b66e88a45fb99c18f10ee65921e9bde3e055d4ab2413e04b0f7`, matching the published 0.2.6 build.
- `READY ... v0.2.6`, followed by the five-second automatic countdown and completion.
- Model gate: `10de:2803`, subsystem `1569:f314`, AD106, `VRAM_MATCH=1` (16,380 MiB register readout).
- After FLR and GOP recovery: `SS0=0x88888888`, `SS1=0x00000008`, `effective=0`, `UNLOCK_MATCHED=1`.
- Final report: `status=0 stage=post-unlock-handoff attempted=1 driver_release=1 clean=1`.
- Safety journal: 2/2 valid slots; latest sequence 75, report sequence 515; no failed phase, I/O fault or log-write error.
- Transaction quiesced; no retained allocations or DMA mappings; firmware-driver ownership released and display reconnected.

The final journal records the clean pre-Windows handoff state. Windows startup
and subsequent workloads are confirmed by the user's report, not by a journal
record written after Windows starts. The FBPA mismatch remains the documented
nonfatal condition; the required payload/readback gates passed.

Known cosmetic issue: the first report line still says `v0.2.5`; the READY line,
automatic-start trace and executable checksum identify 0.2.6. This documentation
update does not change the tested executable. Raw machine logs are not published.

Evidence hashes:

- REPORT.TXT: `7ca9a2f6ce1ac954638106906dcb96d516a96a3002bd192df9d670890a85f0b1`
- SAFETY.LOG: `1b584a8d31a3227260e69a5b63546330b6f8fb7bbbe9d03b77adb541fcc6ffa5`

## User-supplied instruction benchmark

Command: `./mma_bench 100000 20`. Output identifies NVIDIA GeForce RTX 4060 Ti,
34 SMs, sm_89, 100,000 iterations and 20 repetitions. The user reports removing
the reduced power limit; the exact wattage and telemetry were not supplied.

| Mode | Best | Unit |
|---|---:|---|
| f16/f16 | 90.6 | TFLOPS |
| f16/f32 | 90.5 | TFLOPS |
| bf16/f32 | 90.2 | TFLOPS |
| s8/s32 | 181.6 | TOPS |
| fp8/e4m3 | 182.1 | TFLOPS |
| dp4a | 45.5 | TOPS |
| ffma | 21.8 | TFLOPS |

Full supplied samples (the tool uses the generic heading `TF/s`, including integer modes):

```text
mode       best TF/s   all runs
f16/f16         90.6 90.2/90.2/90.1/90.4/90.2/90.3/90.4/90.6/90.5/90.5/90.2/90.5/90.3/90.5/90.4/90.4/90.5/90.1/90.2/90.4
f16/f32         90.5 90.0/90.5/90.5/90.4/90.2/90.4/90.0/89.8/90.2/89.7/90.3/89.5/90.0/89.9/90.0/90.1/90.1/89.9/89.8/90.0
bf16/f32        90.2 89.6/90.1/89.9/90.0/89.7/89.5/90.2/89.8/89.9/89.9/89.9/89.8/89.2/89.7/89.6/90.1/90.0/89.8/89.9/90.1
s8/s32         181.6 179.2/179.5/179.6/179.4/181.6/180.4/181.4/181.4/180.5/180.7/180.0/181.3/181.0/181.3/179.6/180.7/180.4/181.1/181.3/180.5
fp8/e4m3       182.1 180.5/179.9/180.4/180.3/180.2/181.2/182.1/180.7/179.3/178.9/181.2/181.3/181.2/179.2/180.0/181.4/180.9/180.2/180.2/180.3
dp4a            45.5 45.0/45.2/45.2/44.4/45.5/45.0/44.8/45.5/44.1/45.5/45.5/44.8/45.5/44.1/45.5/45.3/44.6/45.5/44.0/45.5
ffma            21.8 21.7/21.8/21.8/21.8/21.7/21.8/21.6/21.4/21.8/20.8/21.6/21.8/21.0/21.8/21.8/21.3/21.7/21.4/21.6/21.8
```

There is no matching stock run for this benchmark, and this output contains no
correctness checks. These rates do not establish a measured 2x local speedup or
long-term stability. The reported PyTorch improvement is qualitative; retain the
original 100 W before/after results as the quantified PyTorch comparison.
