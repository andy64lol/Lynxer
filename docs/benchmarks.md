# Performance benchmarks

Run the process-level benchmark suite from the repository root after building
Lynxer:

```bash
make buildLynxer
make benchLynxer
```

The runner is `benchmarks/bench.py` and uses only Python's standard library.
It measures five cases:

- an arithmetic loop through the interpreter with optimization enabled;
- the same loop with `--no-opt`, to show the optimizer's runtime effect;
- the loop in a self-contained executable made with `--compile`;
- an empty program launched through the interpreter;
- the same empty program launched as a compiled executable.

The empty-program pair estimates the executable startup difference. The
workload cases include process startup, parsing, optimization when enabled, and
execution, so compare them on the same host and build. These are measurements,
not cross-machine scores or CI pass/fail thresholds.

```bash
python3 benchmarks/bench.py \
  --lynxer lynxer/lynxer \
  --runs 15 \
  --warmups 3 \
  --loop-count 250000 \
  --json dist/benchmark.json
```

The JSON report includes each sample, median, 90th percentile, host/build
metadata, and the ratios for optimizer speedup and empty compiled startup.
Increase the run count to reduce noise; close other CPU-heavy processes and
avoid comparing different compiler flags or machines.
