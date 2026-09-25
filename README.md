# Link Scheduling (Part A)

Multithreaded TCP file server with FCFS, SJF, RR, and DRR scheduling, plus a closed-loop load client. Built with `g++ -std=c++17 -pthread`. No external libraries.

## Build

```bash
make
make workload
```

Binaries: `./server`, `./client`. Workload files: `workload/`.

## Config (`config.json`)

Every `server` field is required:

| Field | Meaning |
|---|---|
| `server.ip` | Bind / connect address |
| `server.port` | Bind / connect port |
| `server.server_threads` | Worker threads that drain the scheduler queue |
| `server.client_threads` | Concurrent closed-loop threads used by `./client load` |

`config_1thread.json` is the A28 1-worker config (same client thread count). A
`load_balancer` block is optional for Part A; when present, its `ip`, `port`,
`health_interval_ms`, and exactly four `backends` entries are validated.

Missing or mistyped fields exit non-zero, e.g. `error: missing required field 'server.port'`.

## Server flags

```bash
./server --sched <fcfs|sjf|rr|drr> --file <dir> [--quantum Q] [--p N] \
         [--config path] [--metrics-out path]
```

- `--sched` required.
- `--file` required: directory the server serves and stores into.
- `--quantum Q` required for `rr`/`drr`, rejected for `fcfs`/`sjf`. `Q` is a **byte** allowance per round.
- `--p N` optional; groups up to N whole GET lines per `send`. Default 1. Does not change bytes, order, or quantum accounting.
- `--config` default `config.json`.
- `--metrics-out` default `metrics.csv`. Written on shutdown.

SIGINT/SIGTERM: stop accepting, finish admitted requests (including RR/DRR requeues), print an aggregate summary, write the CSV. The listen socket uses `SO_REUSEADDR`.

## Client

The server address always comes from the config file.

```bash
./client [--config path] put <local-path>
./client [--config path] get <name>
./client [--config path] load <workload-dir> --requests N
```

- `put` uploads a local file; only the **base name** is sent on the wire.
- `get` downloads `<name>` into the current directory under that name.
- `load` seeds the server by PUTting every file in the workload directory once (seeding is not counted), then issues `N` closed-loop requests across `client_threads` threads. Each request picks a file uniformly and GET/PUT with equal probability. `--requests` is required for `load` and rejected otherwise. The client prints no metrics.

## Protocol

Framed, one request per connection:

- `GET <name>\n` → `OK <n>\n` + `n` bytes, or `ERR <reason>\n`
- `PUT <name> <bytes>\n` → `OK 0\n` → body → `OK 0\n`
- `HEALTH\n` → `OK <queue_depth>\n` (answered immediately, never queued, never in the CSV)

## Architecture (A5)

1. Acceptor thread: `accept()` only.
2. Parser pool: header read with a 1s timeout, filename/size checks, HEALTH out of band, then enqueue. A silent client cannot stall admission of others.
3. Shared queue of admitted GET/PUT requests (size known at enqueue).
4. Worker pool: the active policy chooses the next request and a byte budget.

RR/DRR requeue at the tail with offset, leftover socket bytes, rounds, forfeited bytes, and (DRR) deficit preserved. Queue depth for HEALTH is the number of admitted-but-unserved requests, including preempted ones.

## Policies

- **FCFS**: arrival order, one round, full remaining transfer.
- **SJF**: smallest declared byte count (GET file size, PUT header count). No aging.
- **RR**: at most `Q` bytes per round. GET rounds stop before a line that would exceed the remaining allowance; leftover allowance is **forfeited**. A single line longer than `Q` is sent in full (A14) so the request cannot stall. PUT takes up to `Q` bytes exactly.
- **DRR**: same as RR except unused allowance is kept as deficit (`allowance = deficit + Q` at the start of a round). A14 is not used; a line of length `L > Q` is sent once deficit covers it.

Timestamps use `CLOCK_MONOTONIC`. Waiting = start − arrival; response = finish − arrival. Throughput = `N / (max(finish) − min(arrival))`. CSV columns match A23. Percentiles are nearest-rank: `index = ceil(p/100 × N) − 1`.

## Workload (A27)

`scripts/gen_workload.py` writes:

| File | Role |
|---|---|
| `workload/small.txt` | ~1 KB, 60–80 byte lines |
| `workload/medium.txt` | ~30 KB, 60–80 byte lines |
| `workload/large.txt` | ~150 KB, 60–80 byte lines |
| `workload/longline.txt` | 8 × 20 KB lines (`> Q`) |

Reference quantum: **4096 bytes** (in 2–16 KB so large files are preempted many times).

## Reproduce experiments (A28–A29)

```bash
chmod +x scripts/run_part_a.sh
N=1000 ./scripts/run_part_a.sh
python3 scripts/analyze_metrics.py results/*.csv
```

Six runs:

1. `fcfs`, `sjf`, `rr`, `drr` with 4 server threads / 8 client threads (`config.json`)
2. `fcfs` and `rr` with 1 server thread (`config_1thread.json`)

Each run uses at least 1000 load requests (plus uncounted seeding PUTs). CSVs land in `results/`. On shutdown the server also prints waiting p50/p99, throughput, A14 events, and forfeited bytes.

`--p` is not part of the required matrix.

## Design choices

- Header parsing uses a parser thread pool sized to `max(8, client_threads)` so `accept()` never blocks on `recv`.
- After SIGINT, new admissions stop but **requeue is still allowed** so a preempted request is not dropped.
- Bytes already read past the header line are kept on the request and consumed first on PUT (A17).
- GET is line-oriented; PUT is an opaque byte count (A9).
- The load client discards GET bodies (the interactive `get` command still writes them). Metrics come only from the server CSV.
