"""Summarize a native UE CSV capture; timings are milliseconds and inclusive.

Usage: python Scripts/analyze_warehouse_profile.py capture.csv --json-out report.json
No third-party packages. Accepts .csv.gz and UE's final, expanded summary header.
"""
import argparse
import collections
import csv
import gzip
import json
import math
import statistics
from pathlib import Path


SCOPES = {
    "DamageTick", "LoadGraph", "SupportedActors", "SupportTraversal", "RoutePlanning", "TrafficPermit",
    "InventoryRefresh", "InboundScan", "ForkliftTick", "ForkliftMotors",
}
COUNTERS = {
    "LoadGraphBuilds", "SupportTraceCount", "SupportedActorQueries",
    "NavigationQueries", "RoutePlans", "GeometryRejectedCandidates", "SupportSnapshotReuses",
}
NATIVE_TIMINGS = {
    "gamethread", "renderthread", "rhithread", "physics", "physicsthread", "physicstime",
    "gamethreadtime", "renderthreadtime", "rhithreadtime", "gputime",
    "gamethreadtime_criticalpath", "renderthreadtime_criticalpath",
}


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (ValueError, TypeError):
        return None


def percentile(ordered, fraction):
    index = (len(ordered) - 1) * fraction
    low = math.floor(index)
    high = math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def summary(values):
    ordered = sorted(v for v in values if v is not None)
    if not ordered:
        return None
    return {
        "samples": len(ordered), "mean": statistics.fmean(ordered),
        "median": statistics.median(ordered), "p95": percentile(ordered, .95),
        "p99": percentile(ordered, .99), "max": ordered[-1],
    }


def leaf(name):
    return name.rsplit("/", 1)[-1]


def read_capture(path):
    opener = gzip.open if path.suffix.lower() == ".gz" else open
    headers, records, metadata = [], [], {}
    with opener(path, "rt", encoding="utf-8-sig", newline="") as stream:
        for record, row in enumerate(csv.reader(stream), 1):
            if not row or not any(cell.strip() for cell in row):
                continue
            if row[0].strip().upper() == "EVENTS":
                headers.append(row)
            elif row[0].startswith("["):
                for key, value in zip(row[::2], row[1::2]):
                    if key.startswith("[") and key.endswith("]"):
                        metadata[key[1:-1]] = value
            else:
                records.append((record, row))
    if not headers:
        raise ValueError("No UE EVENTS header found")
    # UE appends new series during capture, preserving their column order.
    header = headers[-1]
    if any(header[:len(old)] != old for old in headers):
        raise ValueError("CSV headers changed column order; cannot safely align rows")
    counts = collections.Counter(header)
    aliases = [f"{name} [column {i + 1}]" if counts[name] > 1 else name for i, name in enumerate(header)]
    original_names = dict(zip(aliases, header))
    return aliases, records, metadata, original_names


def analyze(path, skip_frames=0):
    header, records, metadata, original_names = read_capture(path)
    indexes = {name: i for i, name in enumerate(header)}
    find = lambda tail: next((name for name in header if leaf(original_names[name]).lower() == tail.lower()
                             and not name.startswith("COUNTS/")), None)
    frame_name = find("FrameTime")
    sim_name = find("SimulationSeconds")
    speed_name = find("RequestedSpeed")
    if frame_name is None:
        raise ValueError("FrameTime column is missing")
    frames, excluded = [], {"invalid_frame_time": 0, "missing_or_nonpositive_simulation_time": 0, "explicit_skip": skip_frames}
    for capture_frame, (record, raw) in enumerate(records):
        values = [number(raw[i]) if i < len(raw) else None for i in range(len(header))]
        frame_time = values[indexes[frame_name]]
        if frame_time is None or frame_time <= 0:
            excluded["invalid_frame_time"] += 1
            continue
        sim = values[indexes[sim_name]] if sim_name else None
        if sim_name and (sim is None or sim <= 0):
            excluded["missing_or_nonpositive_simulation_time"] += 1
            continue
        frames.append({"capture_frame": capture_frame, "csv_record": record,
                       "events": raw[0], "simulation_seconds": sim, "values": values})
    frames = frames[skip_frames:]
    if not frames:
        raise ValueError("No valid frames remain after the documented exclusions")
    values_of = lambda name: [f["values"][indexes[name]] for f in frames]
    warehouse = lambda name: "Warehouse" in original_names[name].split("/")
    scopes = {name: summary([v or 0. for v in values_of(name)]) for name in header
              if warehouse(name) and leaf(original_names[name]) in SCOPES and not name.startswith("COUNTS/")}
    counters = {name: {"total": sum(v or 0. for v in values_of(name)),
                       "active_frames": sum(bool(v) for v in values_of(name)),
                       "max_per_frame": max(v or 0. for v in values_of(name))}
                for name in header if warehouse(name) and (leaf(original_names[name]) in COUNTERS or name.startswith("COUNTS/"))}
    native = {name: summary(values_of(name)) for name in header
              if not warehouse(name) and not name.startswith("COUNTS/") and leaf(original_names[name]).lower() in NATIVE_TIMINGS}
    frame_values = values_of(frame_name)
    sim_values = values_of(sim_name) if sim_name else []
    speeds = sorted(set(v for v in values_of(speed_name) if v and v > 0)) if speed_name else []
    simulation_elapsed = sim_values[-1] - sim_values[0] if sim_values else None
    wall_estimate = sum(frame_values) / 1000
    budget = 1000 / (30 * speeds[0]) if len(speeds) == 1 else None
    route_name = next((name for name in scopes if leaf(original_names[name]) == "RoutePlanning"), None)
    route_frames = [f for f in frames if route_name and (f["values"][indexes[route_name]] or 0) > 0]

    def frame_detail(frame):
        return {
            "capture_frame": frame["capture_frame"], "csv_record": frame["csv_record"],
            "simulation_seconds": frame["simulation_seconds"],
            "frame_time_ms": frame["values"][indexes[frame_name]], "events": frame["events"],
            "warehouse_timings_ms": {name: frame["values"][indexes[name]] or 0. for name in scopes},
            "warehouse_counts": {name: frame["values"][indexes[name]] or 0. for name in counters},
        }

    warnings = []
    duplicate_columns = {alias: name for alias, name in original_names.items() if alias != name}
    if duplicate_columns:
        warnings.append("Duplicate stat names were kept as separate column-number aliases; no values were combined.")
    if metadata.get("HasHeaderRowAtEnd") != "1":
        warnings.append("Final metadata is absent: capture may still be writing or be incomplete.")
    if sim_values and any(b < a for a, b in zip(sim_values, sim_values[1:])):
        warnings.append("Simulation clock decreased; elapsed time is not a continuous-run comparison.")
    if not scopes:
        warnings.append("Warehouse timing scopes are absent; check the instrumented build and enabled category.")
    unavailable_native = [name for name, stat in native.items() if stat and stat["max"] == 0]
    if unavailable_native:
        warnings.append("All-zero native timing columns may be unavailable in this capture; they do not prove zero work: " + ", ".join(unavailable_native))
    return {
        "input": str(path.resolve()), "numeric_records": len(records), "analyzed_frames": len(frames),
        "duplicate_column_aliases": duplicate_columns,
        "duplicate_column_stats_unknown_units": {name: summary(values_of(name)) for name in duplicate_columns},
        "exclusions": excluded, "frame_time_ms": summary(frame_values),
        "native_timings_ms": native, "unavailable_native_timings": unavailable_native,
        "warehouse_timings_ms": scopes, "warehouse_counts": counters,
        "route_planning_nonzero_ms": summary([f["values"][indexes[route_name]] for f in route_frames]),
        "route_planning_events": [frame_detail(f) for f in route_frames],
        "simulation_elapsed_seconds": simulation_elapsed,
        "wall_elapsed_estimate_seconds": wall_estimate,
        "achieved_speed_estimate": simulation_elapsed / wall_estimate if simulation_elapsed is not None else None,
        "requested_speeds": speeds, "frame_budget_ms_at_fixed_30hz": budget,
        "frames_over_budget": sum(v > budget for v in frame_values) if budget else None,
        "worst_frames": [frame_detail(f) for f in sorted(frames, key=lambda f: f["values"][indexes[frame_name]], reverse=True)[:10]],
        "metadata": metadata, "warnings": warnings,
        "notes": [
            "Timing units are milliseconds. Scopes are inclusive; do not add nested scope means.",
            "FrameTime includes frame pacing/waits. NullRHI captures cannot diagnose rendering or GPU cost.",
            "Invalid/nonpositive FrameTime and absent/nonpositive SimulationSeconds (when present) are excluded; no automatic warmup trim.",
            "Wall elapsed is the sum of retained FrameTime intervals, not a separate wall clock; CSV frame boundaries can be shifted by one frame.",
            "Warehouse scope/count columns not yet registered in early rows contribute zero; native absent samples are omitted.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("--json-out", "-o", type=Path)
    parser.add_argument("--skip-frames", type=int, default=0, help="Explicitly discard this many otherwise valid initial frames")
    args = parser.parse_args()
    if args.skip_frames < 0:
        parser.error("--skip-frames must be nonnegative")
    try:
        report = analyze(args.csv_file, args.skip_frames)
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f"Cannot analyze capture: {error}\n")
    print(f"Frames: {report['analyzed_frames']} / {report['numeric_records']} | excluded: {report['exclusions']}")
    for group in ("frame_time_ms", "native_timings_ms", "warehouse_timings_ms"):
        entries = {"FrameTime": report[group]} if group == "frame_time_ms" else report[group]
        for name, stat in entries.items():
            if stat:
                print(f"{name}: mean={stat['mean']:.3f} median={stat['median']:.3f} p95={stat['p95']:.3f} p99={stat['p99']:.3f} max={stat['max']:.3f} ms")
    for name, stat in report["warehouse_counts"].items():
        print(f"{name}: total={stat['total']:g} active_frames={stat['active_frames']} max/frame={stat['max_per_frame']:g}")
    print(f"Simulation seconds: {report['simulation_elapsed_seconds']} | wall interval estimate: {report['wall_elapsed_estimate_seconds']:.3f}s | requested: {report['requested_speeds']}")
    print(f"Route planning active frames: {len(report['route_planning_events'])}; frames over pacing budget: {report['frames_over_budget']}")
    print("Worst frames (capture frame, milliseconds, simulation seconds):")
    for frame in report["worst_frames"]:
        print(f"  {frame['capture_frame']}: {frame['frame_time_ms']:.3f}, sim={frame['simulation_seconds']}")
    for warning in report["warnings"]:
        print("WARNING:", warning)
    print("Timings are inclusive; do not sum scopes. FrameTime includes pacing. NullRHI does not measure GPU rendering.")
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8")
        print("JSON:", args.json_out.resolve())


if __name__ == "__main__":
    main()
