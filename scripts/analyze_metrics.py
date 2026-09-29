#!/usr/bin/env python3
"""Compute assignment metrics and create Part A tables and figures."""

import argparse
import csv
import math
import re
from collections import defaultdict
from pathlib import Path


CLASSES = ("small", "medium", "large", "longline")
RUN_ORDER = ("fcfs_4t", "sjf_4t", "rr_4t", "drr_4t", "fcfs_1t", "rr_1t")
EXPECTED_HEADER = [
    "request_id", "op", "filename", "bytes", "rounds", "forfeited_bytes",
    "arrival_ns", "start_ns", "finish_ns",
]


def nearest_rank(values, percentile):
    if not values:
        return 0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(percentile / 100 * len(ordered)) - 1)]


def size_class(filename, nbytes):
    name = filename.lower()
    if "small" in name:
        return "small"
    if "medium" in name:
        return "medium"
    if "large" in name:
        return "large"
    if "long" in name:
        return "longline"
    if nbytes <= 2 * 1024:
        return "small"
    if nbytes <= 40 * 1024:
        return "medium"
    return "large"


def read_a14_events(log_path):
    if not log_path.is_file():
        return "missing"
    text = log_path.read_text(errors="replace")
    match = re.search(r"^a14_events:\s*(\d+)\s*$", text, re.MULTILINE)
    return int(match.group(1)) if match else "missing"


def analyze_csv(path):
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != EXPECTED_HEADER:
            raise ValueError(f"{path}: CSV header does not match A23")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path}: no measured requests")

    waiting_ns = []
    finishes = []
    arrivals = []
    slowdown_by_class = defaultdict(list)
    forfeited_by_class = defaultdict(int)
    total_forfeited = 0

    for row in rows:
        arrival = int(row["arrival_ns"])
        start = int(row["start_ns"])
        finish = int(row["finish_ns"])
        nbytes = int(row["bytes"])
        forfeited = int(row["forfeited_bytes"])
        if nbytes <= 0:
            raise ValueError(f"{path}: non-positive transfer size")
        arrivals.append(arrival)
        finishes.append(finish)
        waiting_ns.append(max(0, start - arrival))
        klass = size_class(row["filename"], nbytes)
        slowdown_by_class[klass].append(max(0, finish - arrival) / nbytes)
        forfeited_by_class[klass] += forfeited
        total_forfeited += forfeited

    duration = max(finishes) - min(arrivals)
    run = path.stem
    label = run.replace("_", " ")
    result = {
        "run": run,
        "label": label,
        "requests": len(rows),
        "waiting_p50_ms": nearest_rank(waiting_ns, 50) / 1e6,
        "waiting_p99_ms": nearest_rank(waiting_ns, 99) / 1e6,
        "throughput_rps": len(rows) / (duration / 1e9) if duration else 0,
        "a14_events": read_a14_events(path.with_suffix(".log")),
        "forfeited_bytes": total_forfeited,
    }
    slowdown = {}
    for klass in CLASSES:
        values = slowdown_by_class[klass]
        slowdown[klass] = {
            "count": len(values),
            "median": nearest_rank(values, 50),
            "p99": nearest_rank(values, 99),
            "forfeited_bytes": forfeited_by_class[klass],
        }
    return result, slowdown


def write_csv(path, fieldnames, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


class PdfDocument:
    def __init__(self):
        self.pages = []
        self.commands = []

    def page(self):
        self.commands = []
        self.pages.append(self.commands)

    def text(self, x, y, value, size=10, color=(0.12, 0.16, 0.18)):
        safe = str(value).encode("ascii", "replace").decode("ascii")
        safe = safe.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        r, g, b = color
        self.commands.append(
            f"BT /F1 {size} Tf {r:.3f} {g:.3f} {b:.3f} rg "
            f"1 0 0 1 {x:.2f} {y:.2f} Tm ({safe}) Tj ET"
        )

    def rect(self, x, y, width, height, color):
        r, g, b = color
        self.commands.append(
            f"{r:.3f} {g:.3f} {b:.3f} rg {x:.2f} {y:.2f} {width:.2f} {height:.2f} re f"
        )

    def line(self, x1, y1, x2, y2, color=(0.78, 0.81, 0.82), width=0.6):
        r, g, b = color
        self.commands.append(
            f"{r:.3f} {g:.3f} {b:.3f} RG {width:.2f} w "
            f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S"
        )

    def save(self, path):
        objects = ["", "", "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
        page_ids = []
        for commands in self.pages:
            content = "\n".join(commands).encode("ascii", "replace")
            content_id = len(objects) + 1
            objects.append(
                f"<< /Length {len(content)} >>\nstream\n".encode("ascii")
                + content + b"\nendstream"
            )
            page_id = len(objects) + 1
            objects.append(
                f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
                f"/Resources << /Font << /F1 3 0 R >> >> /Contents {content_id} 0 R >>"
            )
            page_ids.append(page_id)
        objects[0] = f"<< /Type /Catalog /Pages 2 0 R >>"
        kids = " ".join(f"{page_id} 0 R" for page_id in page_ids)
        objects[1] = f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>"

        output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = [0]
        for number, obj in enumerate(objects, start=1):
            offsets.append(len(output))
            output.extend(f"{number} 0 obj\n".encode("ascii"))
            output.extend(obj if isinstance(obj, bytes) else obj.encode("ascii"))
            output.extend(b"\nendobj\n")
        xref = len(output)
        output.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
        output.extend(b"0000000000 65535 f \n")
        for offset in offsets[1:]:
            output.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
        output.extend(
            f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
            f"startxref\n{xref}\n%%EOF\n".encode("ascii")
        )
        path.write_bytes(output)


def heading(pdf, title, subtitle=""):
    pdf.text(42, 800, title, 20, (0.10, 0.32, 0.34))
    if subtitle:
        pdf.text(42, 780, subtitle, 9, (0.35, 0.39, 0.40))
    pdf.line(42, 768, 553, 768, (0.10, 0.48, 0.45), 1.3)


def draw_table(pdf, x, top, headers, rows, widths, row_height=23, font_size=8):
    full_width = sum(widths)
    pdf.rect(x, top - row_height, full_width, row_height, (0.10, 0.32, 0.34))
    cursor = x
    for header, width in zip(headers, widths):
        pdf.text(cursor + 5, top - row_height + 8, header, font_size, (1, 1, 1))
        cursor += width
    y = top - row_height
    for index, row in enumerate(rows):
        y -= row_height
        if index % 2 == 0:
            pdf.rect(x, y, full_width, row_height, (0.93, 0.96, 0.95))
        cursor = x
        for value, width in zip(row, widths):
            pdf.text(cursor + 5, y + 8, value, font_size)
            cursor += width
        pdf.line(x, y, x + full_width, y, (0.82, 0.86, 0.85), 0.4)
    return y


def draw_bars(pdf, title, subtitle, labels, values, x, top, width, height, color,
              logarithmic=False, units=""):
    pdf.text(x, top, title, 13, (0.10, 0.32, 0.34))
    pdf.text(x, top - 16, subtitle, 8, (0.35, 0.39, 0.40))
    base_y = top - height - 32
    plot_top = top - 32
    plot_height = height
    positive = [max(float(v), 0.0001) for v in values]
    transformed = [math.log10(v) if logarithmic else v for v in positive]
    minimum = min(0, min(transformed)) if not logarithmic else min(transformed) - 0.3
    maximum = max(transformed) if not logarithmic else max(transformed) + 0.3
    span = max(1e-9, maximum - minimum)
    slot = width / len(values)
    bar_width = slot * 0.56
    pdf.line(x, base_y, x + width, base_y, (0.35, 0.39, 0.40), 0.7)
    for index, (label, value, shown) in enumerate(zip(labels, values, transformed)):
        bar_height = max(2, (shown - minimum) / span * plot_height)
        bx = x + index * slot + (slot - bar_width) / 2
        pdf.rect(bx, base_y, bar_width, bar_height, color)
        pdf.text(bx - 3, base_y + bar_height + 5, f"{float(value):.2f}{units}", 7)
        pdf.text(x + index * slot + 2, base_y - 15, label, 7)


def write_figure_pdf(runs, slowdowns, out_path):
    pdf = PdfDocument()

    pdf.page()
    heading(pdf, "Part A experiment summary", "N=1000 measured requests per run; seed PUTs excluded")
    summary = [[
        run["label"], str(run["requests"]), f'{run["waiting_p50_ms"]:.2f}',
        f'{run["waiting_p99_ms"]:.2f}', f'{run["throughput_rps"]:.1f}',
        str(run["a14_events"]), str(run["forfeited_bytes"]),
    ] for run in runs]
    draw_table(pdf, 42, 735,
               ["Run", "N", "Wait p50 ms", "Wait p99 ms", "Req/s", "A14", "Forfeit B"],
               summary, [90, 35, 75, 75, 65, 55, 75], 25, 7)
    draw_bars(pdf, "Waiting p50", "Log scale; milliseconds", [r["label"] for r in runs],
              [r["waiting_p50_ms"] for r in runs], 60, 500, 470, 150,
              (0.13, 0.49, 0.46), True)
    pdf.page()
    heading(pdf, "Waiting and throughput", "Throughput uses N / (max finish - min arrival) for each run")
    draw_bars(pdf, "Waiting p99", "Log scale; milliseconds", [r["label"] for r in runs],
              [r["waiting_p99_ms"] for r in runs], 60, 720, 470, 250,
              (0.13, 0.49, 0.46), True)
    draw_bars(pdf, "Throughput", "Requests per second", [r["label"] for r in runs],
              [r["throughput_rps"] for r in runs], 60, 390, 470, 230,
              (0.82, 0.39, 0.21))

    pdf.page()
    heading(pdf, "Normalized slowdown", "Reference configuration: four workers; response time divided by bytes, ns/byte")
    slowdown_rows = []
    for run in slowdowns:
        if not run["run"].endswith("_4t"):
            continue
        for klass in CLASSES:
            slowdown_rows.append([
                run["run"].replace("_4t", "").upper(), klass,
                str(run[f"{klass}_count"]), f'{run[f"{klass}_median"]:.2f}',
                f'{run[f"{klass}_p99"]:.2f}',
            ])
    draw_table(pdf, 55, 730, ["Policy", "Class", "N", "Median ns/B", "p99 ns/B"],
               slowdown_rows, [90, 95, 55, 130, 130], 28, 8)

    pdf.page()
    heading(pdf, "RR versus DRR", "Q=4096 bytes; GET scheduling is line-granular")
    rr = next(run for run in runs if run["run"] == "rr_4t")
    drr = next(run for run in runs if run["run"] == "drr_4t")
    forfeits = []
    by_run = {row["run"]: row for row in slowdowns}
    for klass in CLASSES:
        forfeits.append([
            klass, str(by_run["rr_4t"][f"{klass}_forfeited_bytes"]),
            str(by_run["drr_4t"][f"{klass}_forfeited_bytes"]),
        ])
    draw_table(pdf, 65, 715, ["Class", "RR forfeited bytes", "DRR forfeited bytes"],
               forfeits, [150, 150, 160], 32, 9)
    pdf.text(65, 535, f'RR A14 events: {rr["a14_events"]}', 11)
    pdf.text(65, 515, f'DRR A14 events: {drr["a14_events"]}', 11)
    for klass in ("longline",):
        pdf.text(65, 477, f'{klass} median slowdown: RR {by_run["rr_4t"][f"{klass}_median"]:.2f} ns/B', 10)
        pdf.text(65, 459, f'{klass} median slowdown: DRR {by_run["drr_4t"][f"{klass}_median"]:.2f} ns/B', 10)
    pdf.text(65, 415, "RR forfeits the unused tail when the next whole line exceeds the remaining quantum.", 9)
    pdf.text(65, 398, "A line longer than Q uses A14 and overruns the quantum; DRR retains unused deficit", 9)
    pdf.text(65, 381, "across rounds, avoiding A14 but making long lines wait until their deficit covers them.", 9)
    pdf.save(out_path)


def write_report_pdf(runs, slowdowns, out_path):
    pdf = PdfDocument()
    by_run = {row["run"]: row for row in slowdowns}

    pdf.page()
    heading(pdf, "Link Scheduling: Part A", "Experimental report | Q=4096 bytes | 1000 measured requests per run")
    paragraphs = [
        "Setup. The workload has small (1,024 B), medium (30,720 B), large (153,600 B), and",
        "long-line (160,008 B; eight 20 KB lines) files. Other lines are 60-80 B. The",
        "reference configuration uses four workers and eight client threads; the single-worker",
        "comparison uses one worker and eight client threads. Each run uses a closed-loop load",
        "with a uniform file choice and equal GET/PUT probability. Four seed PUTs are excluded.",
        "",
        "Policies. FCFS serves in arrival order. SJF chooses the smallest declared transfer size",
        "for either verb. RR allows Q bytes per round, rounds GET to whole lines, and discards",
        "unused allowance at preemption. A GET line larger than Q is sent whole under A14.",
        "DRR uses the same quantum but retains unused allowance as a per-request deficit; it",
        "waits for enough accumulated deficit to send an over-quantum line and does not use A14.",
        "On preemption, each request retains its transfer offset, buffered unread body bytes, and",
        "DRR deficit. The CSV captures arrival, first start, completion, rounds, and forfeiture.",
        "",
        "Measurements were collected on the current macOS development environment. The",
        "code is built with the required C++17/POSIX toolchain here; Linux execution should be",
        "confirmed in the submission environment before claiming Linux runtime validation.",
    ]
    y = 735
    for line in paragraphs:
        pdf.text(48, y, line, 10)
        y -= 19 if line else 9

    pdf.page()
    heading(pdf, "A28: waiting and throughput", "Nearest-rank wait percentiles; throughput over the full observation window")
    summary = [[
        run["label"], str(run["requests"]), f'{run["waiting_p50_ms"]:.3f}',
        f'{run["waiting_p99_ms"]:.3f}', f'{run["throughput_rps"]:.2f}',
    ] for run in runs]
    draw_table(pdf, 55, 735,
               ["Run", "N", "Wait p50 ms", "Wait p99 ms", "Throughput req/s"],
               summary, [100, 45, 100, 100, 125], 29, 8)
    draw_bars(pdf, "Waiting p99", "Log scale; milliseconds", [r["label"] for r in runs],
              [r["waiting_p99_ms"] for r in runs], 60, 450, 470, 150,
              (0.13, 0.49, 0.46), True)

    pdf.page()
    heading(pdf, "A21: normalized slowdown", "Reference configuration (4 workers); response / bytes in ns/byte")
    slowdown_rows = []
    for policy in ("fcfs", "sjf", "rr", "drr"):
        row = by_run[f"{policy}_4t"]
        for klass in CLASSES:
            slowdown_rows.append([
                policy.upper(), klass, str(row[f"{klass}_count"]),
                f'{row[f"{klass}_median"]:.2f}', f'{row[f"{klass}_p99"]:.2f}',
            ])
    draw_table(pdf, 55, 735, ["Policy", "Class", "N", "Median ns/B", "p99 ns/B"],
               slowdown_rows, [90, 100, 50, 125, 125], 28, 8)
    pdf.text(55, 290, "Interpretation: small transfers can be penalized when they wait behind large FCFS work.", 9)
    pdf.text(55, 273, "SJF improves typical small-request slowdown, but its high tail wait reflects starvation risk", 9)
    pdf.text(55, 256, "for large requests under a sustained stream of shorter work. RR/DRR reduce queue-wait tails", 9)
    pdf.text(55, 239, "by sharing service, at a throughput/completion-time cost visible in these single runs.", 9)

    pdf.page()
    heading(pdf, "A29: RR versus DRR", "Per-class forfeited bytes and long-line slowdown")
    rr = next(run for run in runs if run["run"] == "rr_4t")
    drr = next(run for run in runs if run["run"] == "drr_4t")
    class_rows = [[
        klass, str(by_run["rr_4t"][f"{klass}_forfeited_bytes"]),
        str(by_run["drr_4t"][f"{klass}_forfeited_bytes"]),
    ] for klass in CLASSES]
    draw_table(pdf, 65, 735,
               ["Size class", "RR forfeited bytes", "DRR forfeited bytes"],
               class_rows, [150, 160, 160], 31, 9)
    pdf.text(65, 555, f'RR A14 events: {rr["a14_events"]}; DRR A14 events: {drr["a14_events"]}.', 11)
    pdf.text(65, 525, f'Long-line median slowdown: RR {by_run["rr_4t"]["longline_median"]:.2f} ns/B;', 10)
    pdf.text(65, 507, f'DRR {by_run["drr_4t"]["longline_median"]:.2f} ns/B.', 10)
    rr_reference = by_run["rr_4t"]
    drr_reference = by_run["drr_4t"]
    pdf.text(
        65, 465,
        f'RR forfeited {rr_reference["medium_forfeited_bytes"]:,} medium and '
        f'{rr_reference["large_forfeited_bytes"]:,} large bytes; long-line forfeiture was '
        f'{rr_reference["longline_forfeited_bytes"]:,}.',
        8,
    )
    pdf.text(65, 448, "Short-line rounds leave unusable quantum tails. Here each 20,001-byte line exceeds Q,", 9)
    pdf.text(65, 431, "so RR sends it whole under A14 rather than forfeiting that round. DRR forfeits no bytes;", 9)
    pdf.text(65, 414, "it carries short-line remainder and waits until deficit covers a long line (up to five", 9)
    pdf.text(65, 397, "rounds here). The measured long-line slowdown reflects this wait. Results are single runs.", 9)
    pdf.text(65, 345, "A14 is counted from per-request GET line sends by the server and preserved in each run log.", 9)
    pdf.save(out_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", help="metrics CSV files")
    parser.add_argument("--out-dir", default=".", help="directory for generated artifacts")
    args = parser.parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = []
    slowdowns = []
    for item in args.csv:
        path = Path(item)
        if not path.is_file():
            raise FileNotFoundError(path)
        run, classes = analyze_csv(path)
        runs.append(run)
        row = {"run": run["run"]}
        for klass in CLASSES:
            for key, value in classes[klass].items():
                row[f"{klass}_{key}"] = value
        slowdowns.append(row)

    expected_runs = {
        "fcfs_4t", "sjf_4t", "rr_4t", "drr_4t", "fcfs_1t", "rr_1t"
    }
    if {run["run"] for run in runs} != expected_runs:
        raise ValueError("provide exactly the six required Part A experiment CSVs")
    if any(run["requests"] < 1000 for run in runs):
        raise ValueError("each Part A experiment must contain at least 1000 measured requests")
    if any(run["a14_events"] == "missing" for run in runs):
        raise ValueError("A14 counts are missing; preserve the corresponding server .log files")

    order = {name: index for index, name in enumerate(RUN_ORDER)}
    runs.sort(key=lambda row: order[row["run"]])
    slowdowns.sort(key=lambda row: order[row["run"]])
    write_csv(out_dir / "part_a_summary.csv", list(runs[0]), runs)
    slowdown_fields = ["run"] + [f"{klass}_{key}" for klass in CLASSES
                                  for key in ("count", "median", "p99", "forfeited_bytes")]
    write_csv(out_dir / "part_a_slowdown.csv", slowdown_fields, slowdowns)

    print("Run summary (waiting milliseconds; throughput requests/second):")
    for run in runs:
        print(
            f'{run["label"]}: N={run["requests"]} wait_p50={run["waiting_p50_ms"]:.3f} '
            f'wait_p99={run["waiting_p99_ms"]:.3f} throughput={run["throughput_rps"]:.3f} '
            f'A14={run["a14_events"]} forfeited={run["forfeited_bytes"]}'
        )
    write_figure_pdf(runs, slowdowns, out_dir / "part_a_figures.pdf")
    write_report_pdf(runs, slowdowns, out_dir / "part_a_report.pdf")
    print(f"Wrote tables, figures, and report to {out_dir.resolve()}")


if __name__ == "__main__":
    main()