"""Offline CUDA baseline: dense GEMM and synthetic Transformer training.

Run the exact same file/environment before and after an experimental change.
No downloads, firmware operations, GPU tuning, or changes to user notebooks.
"""
import argparse
import contextlib
import csv
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

import torch
from torch import nn
from torch.nn import functional as F

SEED = 20261026
CONFIG = dict(sizes=[2048, 4096, 8192], repeats=7, gemm_sample_ms=300,
              batch=16, sequence=128, width=512, heads=8, layers=4,
              vocab=8192, train_warmup=10, train_steps_per_sample=20)
SMI_FIELDS = 'timestamp,name,pstate,temperature.gpu,utilization.gpu,memory.used,power.draw,power.limit,clocks.sm,clocks.mem'


def smi(fields):
    try:
        return subprocess.check_output(
            ['nvidia-smi', '--query-gpu=' + fields, '--format=csv,noheader,nounits'],
            text=True, timeout=5).strip()
    except (OSError, subprocess.SubprocessError) as exc:
        return 'unavailable: ' + str(exc)


def stats(values):
    mean = statistics.mean(values)
    return dict(median_ms=statistics.median(values), min_ms=min(values),
                max_ms=max(values), cv_percent=100 * statistics.pstdev(values) / mean,
                samples_ms=values)


def save(path, data):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
    tmp.replace(path)


class Block(nn.Module):
    def __init__(self):
        super().__init__()
        d = CONFIG['width']
        self.norm1 = nn.LayerNorm(d)
        self.qkv = nn.Linear(d, 3 * d)
        self.proj = nn.Linear(d, d)
        self.norm2 = nn.LayerNorm(d)
        self.ff = nn.Sequential(nn.Linear(d, 4 * d), nn.GELU(), nn.Linear(4 * d, d))

    def forward(self, x):
        b, t, d = x.shape
        h = CONFIG['heads']
        q, k, v = self.qkv(self.norm1(x)).reshape(b, t, 3, h, d // h).permute(2, 0, 3, 1, 4).unbind(0)
        a = F.scaled_dot_product_attention(q, k, v, dropout_p=0.0, is_causal=True)
        x = x + self.proj(a.transpose(1, 2).reshape(b, t, d))
        return x + self.ff(self.norm2(x))


class LanguageModel(nn.Module):
    def __init__(self):
        super().__init__()
        c = CONFIG
        self.embed = nn.Embedding(c['vocab'], c['width'])
        self.position = nn.Parameter(torch.randn(1, c['sequence'], c['width']) * 0.02)
        self.blocks = nn.Sequential(*(Block() for _ in range(c['layers'])))
        self.norm = nn.LayerNorm(c['width'])
        self.head = nn.Linear(c['width'], c['vocab'], bias=False)

    def forward(self, tokens):
        return self.head(self.norm(self.blocks(self.embed(tokens) + self.position)))


def gemm(dtype, size):
    torch.manual_seed(SEED + size)
    a = torch.randn(size, size, device='cuda', dtype=dtype)
    b = torch.randn_like(a)
    out = torch.empty_like(a)

    def run():
        torch.mm(a, b, out=out)

    for _ in range(10):
        run()
    start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
    start.record()
    for _ in range(10):
        run()
    end.record()
    end.synchronize()
    iterations = max(5, min(2000, math.ceil(CONFIG['gemm_sample_ms'] / (start.elapsed_time(end) / 10))))
    samples = []
    for _ in range(CONFIG['repeats']):
        start.record()
        for _ in range(iterations):
            run()
        end.record()
        end.synchronize()
        samples.append(start.elapsed_time(end) / iterations)

    # Full inner dimension, sampled rows/columns, FP64 reference on the CPU.
    indices = torch.linspace(0, size - 1, 16, device='cuda').long()
    reference = a.index_select(0, indices).cpu().double() @ b.index_select(1, indices).cpu().double()
    measured = out.index_select(0, indices).index_select(1, indices).cpu().double()
    relative_l2 = ((measured - reference).norm() / reference.norm()).item()
    tolerance = {torch.float32: 1e-5, torch.float16: 0.001, torch.bfloat16: 0.008}[dtype]
    valid = torch.isfinite(out).all().item() and relative_l2 < tolerance
    result = dict(name=f'gemm_{str(dtype).split(".")[-1]}_{size}', kind='gemm',
                  dtype=str(dtype), size=size, iterations_per_sample=iterations,
                  timing=stats(samples), correctness=dict(passed=valid, relative_l2=relative_l2,
                  relative_l2_limit=tolerance, reference='FP64 CPU, 16x16 evenly spaced output elements'))
    result['tflops'] = (2 * size ** 3) / (result['timing']['median_ms'] * 1e9)
    return result


def training(dtype):
    torch.manual_seed(SEED)
    model = LanguageModel().cuda().train()
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4, foreach=True, fused=False)
    scaler = torch.amp.GradScaler('cuda', enabled=dtype == torch.float16, init_scale=1024)
    tokens = torch.randint(CONFIG['vocab'], (CONFIG['batch'], CONFIG['sequence'] + 1), device='cuda')
    inputs, targets = tokens[:, :-1], tokens[:, 1:].contiguous()

    def autocast():
        return torch.autocast('cuda', dtype=dtype) if dtype != torch.float32 else contextlib.nullcontext()

    def step():
        optimizer.zero_grad(set_to_none=True)
        with autocast():
            logits = model(inputs)
            loss = F.cross_entropy(logits.reshape(-1, CONFIG['vocab']), targets.reshape(-1))
        scaler.scale(loss).backward()
        scaler.step(optimizer)
        scaler.update()
        return loss.detach()

    with torch.no_grad():
        reference_loss = F.cross_entropy(model(inputs).reshape(-1, CONFIG['vocab']), targets.reshape(-1)).item()
        with autocast():
            initial_loss = F.cross_entropy(model(inputs).reshape(-1, CONFIG['vocab']), targets.reshape(-1)).item()
    torch.cuda.reset_peak_memory_stats()
    for _ in range(CONFIG['train_warmup']):
        loss = step()
    torch.cuda.synchronize()
    gpu_samples, wall_samples, losses = [], [], []
    for _ in range(CONFIG['repeats']):
        start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        torch.cuda.synchronize()
        wall_start = time.perf_counter()
        start.record()
        for _ in range(CONFIG['train_steps_per_sample']):
            loss = step()
        end.record()
        torch.cuda.synchronize()
        wall_samples.append(1000 * (time.perf_counter() - wall_start) / CONFIG['train_steps_per_sample'])
        gpu_samples.append(start.elapsed_time(end) / CONFIG['train_steps_per_sample'])
        losses.append(loss.item())
    expected_steps = CONFIG['train_warmup'] + CONFIG['repeats'] * CONFIG['train_steps_per_sample']
    actual_steps = [int(s['step'].item()) for s in optimizer.state.values() if 'step' in s]
    finite = all(torch.isfinite(p).all().item() and (p.grad is None or torch.isfinite(p.grad).all().item())
                 for p in model.parameters())
    valid = (finite and all(math.isfinite(x) for x in losses) and losses[-1] < initial_loss
             and abs(reference_loss - initial_loss) < 0.05
             and bool(actual_steps) and min(actual_steps) == max(actual_steps) == expected_steps)
    timing = stats(wall_samples)
    return dict(name=f'train_{str(dtype).split(".")[-1]}', kind='training', dtype=str(dtype),
                parameters=sum(p.numel() for p in model.parameters()), timing=timing,
                cuda_timing=stats(gpu_samples), steps_per_second=1000 / timing['median_ms'],
                tokens_per_second=CONFIG['batch'] * CONFIG['sequence'] * 1000 / timing['median_ms'],
                peak_allocated_mib=torch.cuda.max_memory_allocated() / 2**20,
                correctness=dict(passed=valid, fp32_initial_loss=reference_loss,
                                 initial_loss=initial_loss, final_loss=losses[-1], sample_losses=losses,
                                 finite_parameters_and_gradients=finite, expected_steps=expected_steps,
                                 optimizer_steps_min=min(actual_steps), optimizer_steps_max=max(actual_steps),
                                 final_grad_scale=scaler.get_scale()))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    if not torch.cuda.is_available():
        raise RuntimeError('CUDA is unavailable')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cuda.matmul.allow_fp16_reduced_precision_reduction = False
    torch.backends.cuda.matmul.allow_bf16_reduced_precision_reduction = False
    if hasattr(torch.backends.cuda.matmul, 'allow_fp16_accumulation'):
        torch.backends.cuda.matmul.allow_fp16_accumulation = False
    torch.backends.cudnn.benchmark = False
    props = torch.cuda.get_device_properties(0)
    result = dict(schema_version=1, started_utc=datetime.now(timezone.utc).isoformat(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), config=CONFIG,
                  environment=dict(python=platform.python_version(), platform=platform.platform(),
                  torch=torch.__version__, cuda=torch.version.cuda, cudnn=torch.backends.cudnn.version(),
                  gpu=props.name, capability=list(torch.cuda.get_device_capability()),
                  gpu_memory_bytes=props.total_memory, sm_count=props.multi_processor_count,
                  driver=smi('driver_version'), power_settings=smi('power.limit,power.default_limit,power.min_limit,power.max_limit'),
                  torch_config=torch.__config__.show(),
                  num_threads=torch.get_num_threads(), tf32=False, reduced_precision_reduction=False,
                  fp16_accumulation=False if hasattr(torch.backends.cuda.matmul, 'allow_fp16_accumulation') else 'API unavailable',
                  cuda_visible_devices=os.environ.get('CUDA_VISIBLE_DEVICES'),
                  nvidia_smi_before=smi(SMI_FIELDS)), cases=[], status='running')
    stop = threading.Event()

    def telemetry():
        with (args.output / 'telemetry.csv').open('w', encoding='utf-8') as f:
            f.write('host_utc,case,' + SMI_FIELDS + '\n')
            while not stop.is_set():
                f.write(datetime.now(timezone.utc).isoformat() + ',' + current_case[0] + ',' + smi(SMI_FIELDS) + '\n')
                f.flush()
                stop.wait(1)

    current_case = ['warmup']
    monitor = threading.Thread(target=telemetry, daemon=True)
    monitor.start()
    try:
        save(args.output / 'results.json', result)
        print(json.dumps(result['environment'], indent=2), flush=True)
        # Short sustained warmup before recording any case.
        a = torch.randn(4096, 4096, device='cuda', dtype=torch.float16)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            for _ in range(10):
                torch.mm(a, a)
            torch.cuda.synchronize()
        del a
        dtypes = [torch.float32, torch.float16, torch.bfloat16]
        if not torch.cuda.is_bf16_supported():
            raise RuntimeError('BF16 is unavailable')
        for size in CONFIG['sizes']:
            for dtype in dtypes:
                current_case[0] = f'gemm_{dtype}_{size}'
                case = gemm(dtype, size)
                result['cases'].append(case)
                save(args.output / 'results.json', result)
                print(json.dumps(case), flush=True)
                if not case['correctness']['passed']:
                    raise RuntimeError('GEMM correctness check failed')
        for dtype in dtypes:
            current_case[0] = f'train_{dtype}'
            torch.cuda.empty_cache()
            case = training(dtype)
            result['cases'].append(case)
            save(args.output / 'results.json', result)
            print(json.dumps(case), flush=True)
            if not case['correctness']['passed']:
                raise RuntimeError('Training correctness check failed')
        result['status'] = 'complete'
    except BaseException as exc:
        result['status'] = 'failed'
        result['error'] = repr(exc)
        raise
    finally:
        stop.set()
        monitor.join(timeout=6)
        result['ended_utc'] = datetime.now(timezone.utc).isoformat()
        result['environment']['nvidia_smi_after'] = smi(SMI_FIELDS)
        save(args.output / 'results.json', result)
        with (args.output / 'summary.csv').open('w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(['case', 'median_ms', 'cv_percent', 'tflops', 'steps_per_second', 'correctness'])
            for case in result['cases']:
                writer.writerow([case['name'], case['timing']['median_ms'], case['timing']['cv_percent'],
                                 case.get('tflops', ''), case.get('steps_per_second', ''), case['correctness']['passed']])
    if args.compare:
        baseline = json.loads(args.compare.read_text(encoding='utf-8'))
        mismatches = [key for key in ['script_sha256', 'config'] if baseline[key] != result[key]]
        for key in ['torch', 'cuda', 'cudnn', 'gpu', 'driver', 'power_settings', 'capability', 'num_threads', 'tf32',
                    'reduced_precision_reduction', 'fp16_accumulation']:
            if baseline['environment'][key] != result['environment'][key]:
                mismatches.append('environment.' + key)
        if baseline['status'] != 'complete':
            mismatches.append('baseline.status')
        old = {c['name']: c for c in baseline['cases']}
        comparison = dict(configuration_matches=not mismatches, mismatches=mismatches,
                          speedups={c['name']: old[c['name']]['timing']['median_ms'] / c['timing']['median_ms']
                                    for c in result['cases'] if c['name'] in old})
        save(args.output / 'comparison.json', comparison)
        print(json.dumps(comparison, indent=2), flush=True)


if __name__ == '__main__':
    main()
