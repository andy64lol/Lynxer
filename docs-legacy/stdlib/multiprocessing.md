# multiprocessing

Run shell commands in parallel using Python's `multiprocessing` and `threading` modules.

## Import

```c
global setup(){
    import("multiprocessing");
}
```

## Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `workerCount` | `workerCount()` | Number of CPU cores available |
| `runParallel` | `runParallel(list commands)` | Run shell commands in parallel (thread pool); return list of stdout strings |
| `mapShell` | `mapShell(str template, list items)` | Run a command per item (replace `{}` with each item); return outputs |
| `threadMap` | `threadMap(str template, list items)` | Alias for `mapShell` |
| `runParallelSilent` | `runParallelSilent(list commands)` | Run in parallel, discard output; return list of integer exit codes |
| `runParallelProcess` | `runParallelProcess(list commands)` | Like `runParallel` but uses a process pool — for CPU-bound tasks |

### Thread pool vs process pool

- `runParallel`, `mapShell`, `threadMap`, and `runParallelSilent` use **threads** (`ThreadPoolExecutor`). This is the right choice for shell commands, which spawn separate OS processes — the GIL is never held during the actual work.
- `runParallelProcess` uses **processes** (`ProcessPoolExecutor`) via picklable top-level workers in `lynxer/_mp_workers.py`. Use it when the shell commands are CPU-heavy Python scripts and you want true process isolation.

## Examples

```c
global setup(){
    import("multiprocessing");
}

global main(){
    // Check available cores
    int cores = global.multiprocessing.workerCount();
    print("cores: "); print(cores); print("\n");

    // Run three pings in parallel
    any cmds = seqFromTo(0, -1, 1);
    cmds = listPush(cmds, "echo one");
    cmds = listPush(cmds, "echo two");
    cmds = listPush(cmds, "echo three");

    any outputs = global.multiprocessing.runParallel(cmds);
    print(strOf(outputs)); print("\n");

    // Map a command over a list of values
    any nums = seqFromTo(1, 4, 1);
    any results = global.multiprocessing.mapShell("echo item_{}", nums);
    print(strOf(results)); print("\n");
}
```

## Notes

- `runParallel` and `runParallelSilent` use a process pool (one process per command, capped at CPU count).
- `threadMap` uses a thread pool — lower overhead, better suited for network/file I/O.
- All functions fall back to sequential execution if the process pool cannot be spawned (e.g., when already inside a worker process).
- Each command has a 60-second timeout. Commands that time out return an error string / exit code 1.
- `mapShell` replaces `{}` in the template with the string form of each list element.
