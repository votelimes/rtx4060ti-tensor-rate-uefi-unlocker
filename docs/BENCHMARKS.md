# Benchmark methodology

`benchmarks/benchmark_gpu.py` is the same Python source used for the measured
PyTorch before/after comparison. Run it in an existing CUDA-enabled PyTorch
environment, with no competing GPU workload. It writes only a new output folder.

```sh
python3 benchmarks/benchmark_gpu.py --output /path/to/before
# After an authorized unlock boot, without an intervening reboot:
python3 benchmarks/benchmark_gpu.py --output /path/to/after --compare /path/to/before/results.json
```

Keep GPU, driver, PyTorch, CUDA precision settings, power cap, clock policy and
script identical. `configuration_matches` must be true. Comparing 100 W stock to
165 W unlocked would confound the result. This script does not change power limits.

The test runs 2048/4096/8192 GEMMs in FP32/FP16/BF16, CUDA-event timings with warmup
and seven repeats, and a synthetic 21,064,704-parameter Transformer training test.
Training includes forward, backward and AdamW, using data already on the GPU.
FP16/BF16 training uses autocast; TF32 is disabled. Full FP16 accumulation and
reduced-precision reductions are disabled. Training throughput uses synchronized
wall time. GEMM output type follows input type; this is not the register-resident
instruction benchmark and should not be compared to its peak rates.

GEMM checks sampled outputs against an FP64 CPU reference and checks all output
values for finiteness. Training checks loss, gradients, parameters and optimizer
step counts. These short tests do not prove general numerical accuracy or training
quality on a real dataset. Telemetry captures temperature, power and clocks.

The separate [upstream instruction probe](https://github.com/JustAResearcher/gpu-unlock-ada/tree/main/diagnostics/bench)
uses `tensor_modes_issue.cu` and `tensor_modes_driver_runner.py`, 20,000 iterations
and three repetitions, with 270,336 all-ones outputs checked per mode. It was built
locally with CUDA 13.1.115 for sm_89. Its 100 W after-unlock results are included in
measurements.json. No local stock run exists for this probe. Its simple all-ones
check does not establish general numerical accuracy.

Measurements are from one physical board and one successful unlock boot. Larger
instruction-rate gains do not imply proportional end-to-end training gains.
