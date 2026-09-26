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
  local cfg="$ROOT/config.json"

  if [[ "$threads" == "1" ]]; then
    cfg="$ROOT/config_1thread.json"
  fi
  if [[ "$policy" == "rr" || "$policy" == "drr" ]]; then
    cmd+=(--quantum "$Q")
  fi
  cmd+=(--file "$SERVER_DIR" --config "$cfg" --metrics-out "$metrics")

  mkdir -p "$ROOT/results"
  echo "=== $tag (N=$N, Q=$Q) ==="

  "${cmd[@]}" &
  local pid=$!

  for _ in $(seq 1 200); do
    if python3 - "$cfg" <<'PY'
import json, socket, sys
try:
    with open(sys.argv[1]) as f:
        cfg = json.load(f)
    host = cfg["server"]["ip"]
    port = int(cfg["server"]["port"])
    s = socket.socket()
    s.settimeout(0.2)
    s.connect((host, port))
    s.close()
    raise SystemExit(0)
except Exception:
    raise SystemExit(1)
PY
    then
      break
    fi
    sleep 0.05
  done

  set +e
  ./client --config "$cfg" load "$ROOT/workload" --requests "$N"
  local client_rc=$?
  set -e

  kill -INT "$pid" 2>/dev/null || true
  wait "$pid" || true

  if [[ "$client_rc" -ne 0 ]]; then
    echo "client failed for $tag" >&2
    exit 1
  fi

  python3 scripts/analyze_metrics.py "$metrics"
}

mkdir -p results
run_one fcfs 4
run_one sjf 4
run_one rr 4
run_one drr 4
run_one fcfs 1
run_one rr 1

echo "All Part A runs finished. CSVs are in results/"
