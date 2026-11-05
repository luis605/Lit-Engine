# Headless CLI Reference

`Headless` (`src/Headless/main.cpp`) runs the simulation without a window.

```
Headless [options]
```

| Option | Meaning |
| --- | --- |
| `--scene <file>` | Load a `.litscene` file before running. |
| `--testscene <name>` | Build a built-in test scene. Its suggested tick count is used unless `--ticks` is given. |
| `--ticks <n>` | Ticks to run. Default 600. With `--replay` and no `--ticks`, runs to the end of the recording. |
| `--dt <seconds>` | Step per tick. Default 0, which uses the engine fixed timestep. |
| `--seed <n>` | Random seed. Default 1. Overridden by the seed stored in a replay. |
| `--params <file>` | Load a parameter JSON file into the param bus. |
| `--record <file>` | Record input to a replay file. |
| `--replay <file>` | Play back a replay file. |
| `--hash-interval <n>` | With `--record`, store a world-state hash checkpoint every `n` ticks. |
| `--verify-replay <file>` | Play back a replay and compare its recorded hashes; prints `MISMATCH tick ...` and exits 1 on any mismatch or missing checkpoint. See [Determinism.md](Determinism.md). |
| `--dump <file>` | Write the final state as JSON. |
| `--perf <file>` | Write per-frame timings as CSV. |
| `--metrics <file>` | Write test scene metrics as JSON (only with `--testscene`). |
| `--list-scenes` | Print the test scene names, one per line, and exit 0. |
| `-h`, `--help` | Print usage to stdout and exit 0. |

## Output

On success stdout gets `ticks <n> entities <n> hash <h>`, where the hash covers the final state dump. With a test scene it also prints `scene <name>` and one `metric <name> <value>` line per metric.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success. |
| 1 | Runtime failure: scene, params or replay failed to load, unknown test scene, or the dump or metrics file could not be written. |
| 2 | Usage error: unknown option (usage goes to stderr). |

Identical options, seed and inputs always give the same hash.
