# Link Scheduling (Part A)

A multithreaded C++17 TCP text-file server and client implementing FCFS, SJF, round robin (RR), and deficit round robin (DRR). The server admits requests to a shared queue before workers serve them. It uses POSIX sockets and threads; no external networking or concurrency libraries are required.

> Assignment 2 (Part A) for *Advanced Computer Networks* (COL724), IIT Delhi.

## Results at a glance

1000 measured requests per run, quantum Q = 4096 bytes, 8 client threads (from [`report/part_a_summary.csv`](report/part_a_summary.csv)):

| Policy | Server threads | Waiting p50 (ms) | Waiting p99 (ms) | Throughput (req/s) |
|---|---|---|---|---|
| FCFS | 4 | 0.006 | 123.84 | 74.3 |
| SJF | 4 | 0.005 | 170.44 | 77.2 |
| RR | 4 | 0.004 | 15.03 | 35.4 |
| DRR | 4 | 0.004 | 13.83 | 43.6 |
| FCFS | 1 | 21.845 | 277.59 | 74.6 |
| RR | 1 | 3.056 | 18.15 | 41.6 |

RR and DRR cut tail waiting time by roughly an order of magnitude, at the cost of throughput. The full analysis is in [`report/part_a_report.pdf`](report/part_a_report.pdf).

## Project structure

```
.
├── src/                  # Server, client, scheduler, queue, protocol, metrics
├── tests/
│   └── test_config.cpp   # Config loader smoke test
├── scripts/
│   ├── gen_workload.py   # Generates workload/
│   ├── run_part_a.sh     # Runs the six-experiment matrix
│   └── analyze_metrics.py
├── workload/             # Generated request files
├── results/              # Per-run metrics CSVs
├── report/               # Summary CSVs, figures and report (PDF)
├── config.json           # 4 server threads / 8 client threads
├── config_1thread.json   # 1 server thread / 8 client threads
└── Makefile
```

## Requirements

- Linux (or WSL), since the code uses POSIX sockets and pthreads
- `g++` with C++17 support, `make`
- Python 3 (standard library only) for the scripts

## Build and Run

Build with the system C++ compiler:

```sh
make          # builds ./server and ./client
make test     # builds and runs the config loader test
```

Generate the assignment workload:

```sh
make workload
```

Start a server (FCFS example):

```sh
./server --sched fcfs --file workload --config config.json --metrics-out metrics.csv
```

RR and DRR require a byte quantum. FCFS and SJF reject `--quantum`:

```sh
./server --sched rr --quantum 4096 --file workload --config config.json --metrics-out metrics.csv
```

Stop with SIGINT or SIGTERM. The server stops accepting, drains admitted requests and their requeues, prints a summary, and writes the per-request CSV.

## Configuration

`config.json` contains the required `server` object:

| Field | Meaning |
|---|---|
| `server.ip` | Address the server binds to and the client connects to |
| `server.port` | TCP port for the server |
| `server.server_threads` | Number of worker threads draining the scheduling queue |
| `server.client_threads` | Number of concurrent closed-loop threads used by the load client |

`config_1thread.json` is the A28 configuration with one server worker and eight client threads. Missing fields, incorrect types, malformed JSON, and invalid values are reported as errors. The project does not implement Part B's load balancer.

## Command-Line Interface

Server:

```sh
./server --sched <fcfs|sjf|rr|drr> --file <directory> [--quantum Q] [--p N] [--config path] [--metrics-out path]
```

`--sched` and `--file` are required. `--quantum Q` is required for RR/DRR and is their per-round byte allowance. `--p N` defaults to 1 and groups up to N whole GET lines into one write operation; it does not change bytes, order, or scheduling accounting. `--config` defaults to `config.json`; `--metrics-out` defaults to `metrics.csv`.

The experiment harness uses `--metrics-skip N` to omit the initial N completed requests from metrics. Since the load client seeds the server sequentially before measured requests begin, the harness sets N to the number of workload files. Do not use this option for ordinary standalone runs unless intentionally excluding initial transfers.

Client:

```sh
./client [--config path] put <local-path>
./client [--config path] get <server-filename>
./client [--config path] load <workload-directory> --requests N
```

PUT sends only the local path's base name. GET writes the result to the current directory under the requested name. `load` first PUTs each workload file once as uncounted seeding, then issues N requests using a shared counter across `client_threads`. Each request chooses a file uniformly and chooses GET or PUT with equal probability. Each client thread waits for a complete response before issuing its next request. `--requests` is required for `load` and rejected for other operations. Load metrics are written by the server, not printed by the client.

## Wire Protocol

Each connection carries one request. Headers are newline-terminated; transfer bodies are byte-counted. The server closes the connection after the response.

| Request | Successful exchange |
|---|---|
| `GET <name>\n` | `OK <n>\n` followed by exactly n file bytes |
| `PUT <name> <bytes>\n` | Server sends `OK 0\n`, client sends exactly the declared bytes, server confirms with `OK 0\n` |
| `HEALTH\n` | `OK <queue_depth>\n`; answered immediately outside the scheduling queue and omitted from metrics |

Failures return `ERR <reason>\n`. Filenames containing `/`, or equal to `.` or `..`, are rejected. GET preserves blank lines and a final unterminated line. PUT bodies are opaque bytes.

## Scheduling and Metrics

The parser pool reads and validates headers with a receive timeout before enqueueing, so request size is known at admission and a silent client cannot occupy the admission path indefinitely. HEALTH is handled out of band. Workers share one explicit request queue.

- **FCFS:** serve in arrival order.
- **SJF:** choose the smallest declared byte count for either GET or PUT. There is no aging, so starvation of large transfers is possible.
- **RR:** serve at most Q bytes per round. GET rounds end only at whole-line boundaries; a line that does not fit in the remaining allowance is deferred, and the unused remainder is forfeited. A line longer than Q is sent whole under A14. PUT consumes up to Q bytes exactly.
- **DRR:** use allowance `deficit + Q` and retain unused allowance for the request's next round. It does not use A14; a long line waits until accumulated deficit can cover it.

On preemption, request offsets, any body bytes read beyond the header, and (for DRR) deficit are retained. GET byte counts include newline bytes. The `--p` option affects batching only.

The A23 CSV header is exactly:

```text
request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns
```

Timestamps use `CLOCK_MONOTONIC`. Waiting is `start - arrival`; response is `finish - arrival`. Throughput is `N / (max(finish) - min(arrival))`. Percentiles use nearest rank, `ceil(p/100 * N) - 1`. Normalized slowdown is response divided by bytes in nanoseconds per byte.

## Workload and Experiments

The workload generator creates four files:

| File | Size and line pattern |
|---|---|
| `workload/small.txt` | 1,024 bytes; ordinary short lines |
| `workload/medium.txt` | 30,720 bytes; ordinary short lines |
| `workload/large.txt` | 153,600 bytes; ordinary short lines |
| `workload/longline.txt` | 160,008 bytes; eight 20,001-byte lines, each longer than Q |

The reference quantum is **4096 bytes**. The required six-run matrix is four policies with four server workers and eight client threads, plus FCFS and RR with one server worker and eight client threads. Each run has 1000 measured requests; the four seed transfers are excluded from CSVs and summaries.

Run all six experiments, analyze the CSVs, and regenerate the figures and report:

```sh
N=1000 ./scripts/run_part_a.sh
```

The harness builds the binaries, regenerates the workload, runs the six cases, writes each CSV and server log into `results/`, and then invokes the analyzer. To analyze existing results without rerunning experiments:

```sh
python3 scripts/analyze_metrics.py results/*.csv --out-dir report
```

The analyzer uses only Python's standard library. It verifies the six expected run names, at least 1000 CSV rows per run, and the presence of each log's exact A14 count. It creates:

- `part_a_summary.csv`: waiting p50/p99, throughput, A14 events, forfeited bytes, and request counts.
- `part_a_slowdown.csv`: per-class median and p99 normalized slowdown and forfeited bytes.
- `part_a_figures.pdf`: plots and tables for the experiment results.
- `part_a_report.pdf`: four-page Part A experimental report.

The CSVs in `results/` are kept as the experiment data. The summaries, figures and report are written to `report/`. `--p` is not varied in the required experiment matrix.