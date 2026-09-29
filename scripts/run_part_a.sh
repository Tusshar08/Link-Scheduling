#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make all
python3 scripts/gen_workload.py

Q=4096
N="${N:-1000}"
SERVER_DIR="$ROOT/workload"

run_one() {
  local policy="$1"
  local threads="$2"
  local cmd=(./server --sched "$policy")
  local tag="${policy}_${threads}t"
  local metrics="$ROOT/results/${tag}.csv"
  local log="$ROOT/results/${tag}.log"
  local cfg="$ROOT/config.json"
  local seed_count

  if [[ "$threads" == "1" ]]; then
    cfg="$ROOT/config_1thread.json"
  fi
  if [[ "$policy" == "rr" || "$policy" == "drr" ]]; then
    cmd+=(--quantum "$Q")
  fi
  seed_count="$(find "$SERVER_DIR" -maxdepth 1 -type f ! -name '.*' | wc -l | tr -d '[:space:]')"
  cmd+=(--file "$SERVER_DIR" --config "$cfg" --metrics-out "$metrics"
        --metrics-skip "$seed_count")

  mkdir -p "$ROOT/results"
  echo "=== $tag (N=$N, Q=$Q) ==="

  "${cmd[@]}" >"$log" 2>&1 &
  local pid=$!

  for _ in {1..200}; do
    if grep -q "Server listening" "$log"; then
      break
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      cat "$log" >&2
      echo "server exited before becoming ready for $tag" >&2
      exit 1
    fi
    sleep 0.05
  done
  if ! grep -q "Server listening" "$log"; then
    kill -INT "$pid" 2>/dev/null || true
    wait "$pid" || true
    cat "$log" >&2
    echo "server did not become ready for $tag" >&2
    exit 1
  fi

  set +e
  ./client --config "$cfg" load "$ROOT/workload" --requests "$N"
  local client_rc=$?
  set -e

  kill -INT "$pid" 2>/dev/null || true
  wait "$pid" || true
  cat "$log"

  if [[ "$client_rc" -ne 0 ]]; then
    echo "client failed for $tag" >&2
    exit 1
  fi

}

mkdir -p results
run_one fcfs 4
run_one sjf 4
run_one rr 4
run_one drr 4
run_one fcfs 1
run_one rr 1

python3 scripts/analyze_metrics.py results/*.csv --out-dir .

echo "All Part A runs finished. CSVs are in results/"
