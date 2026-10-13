"""Capture native CSV timings during the existing saved-warehouse regression.

Run through SimulationToolsBootstrap with -PerformanceLabel=<unique label>
and -FleetSimulationSpeed4. No actor/physics shortcuts and no map saving.
CSV scopes are inclusive: nested timings must not be added together.
Optional -PerformanceDuration=90 captures only that initial simulation slice;
it reports a limited profile, never a completed full-workflow test.
"""
import re
import time
import csv
import json
from pathlib import Path
import unreal

command = unreal.SystemLibrary.get_command_line()
match = re.search(r"-PerformanceLabel=([A-Za-z0-9_-]+)", command, re.I)
assert match, "Pass a unique -PerformanceLabel"
label = match.group(1)
duration_match = re.search(r"-PerformanceDuration=([0-9.]+)", command, re.I)
duration = float(duration_match.group(1)) if duration_match else None
assert duration is None or 0 < duration <= 120, "Timed profile duration must be within 120 simulation seconds"
filename = "Warehouse_" + label
saved = Path(unreal.Paths.project_saved_dir()).resolve()
csv_file = saved / "Profiling" / "CSV" / (filename + ".csv")
assert not csv_file.exists(), "Keep the previous capture; choose a new label"

fixture = Path(__file__).with_name("verify_fleet_workflow_pie.py")
namespace = {"__file__": str(fixture), "__name__": "__main__"}
exec(compile(fixture.read_text(encoding="utf-8-sig"), str(fixture), "exec"), namespace)
unreal.unregister_slate_post_tick_callback(namespace["handle"])
original_finish = namespace["finish"]
pending = None
capturing = False
finished_size = None
finished_at = None
verify_startup = "-performanceverifystartup" in command.lower()
startup_reference = {}
verified_times = set()
if verify_startup:
    reference = saved / "Testing" / "FleetWorkflow" / "PerformanceBaselineHeadless1_CaptureCleanupError.json"
    startup_reference = {row["t"]: row for row in json.loads(reference.read_text(encoding="utf-8"))["samples"] if row["t"] <= 25}


def complete_profile():
    print("WAREHOUSE_PROFILE_CAPTURED", str(csv_file))
    if duration is None:
        original_finish(pending[0], pending[1])
        return
    limited = {"ok": pending[0], "detail": pending[1], "scope": "initial timed saved-warehouse slice; full jobs not verified",
               "simulation_seconds": namespace["clock"], "requested_speed": namespace["simulation_speed"],
               "wall_seconds": time.monotonic() - namespace["started"], "events": namespace["report"]["events"],
               "samples": namespace["report"]["samples"]}
    destination = saved / "Testing" / (filename + ".json")
    destination.write_text(json.dumps(limited, ensure_ascii=False, indent=2), encoding="utf-8")
    print("WAREHOUSE_PROFILE_LIMITED_PASSED" if pending[0] else "WAREHOUSE_PROFILE_LIMITED_FAILED", label)
    unreal.unregister_slate_post_tick_callback(namespace["handle"])
    namespace["level"].editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def finish_profile(ok, detail):
    global pending
    if pending is not None:
        return
    pending = (ok, detail, time.monotonic())
    unreal.SystemLibrary.execute_console_command(namespace["game"], "csvprofile stop", namespace["pc"])
    print("WAREHOUSE_PROFILE_STOP", label, ok, "simulation", namespace["clock"])


def tick(delta):
    global capturing, finished_size, finished_at
    if pending is not None:
        # CsvProfile stop is deferred to EndFrame and writes on its own thread.
        age = time.monotonic() - pending[2]
        if age > 30:
            original_finish(False, "CSV capture was not written")
            return
        if csv_file.is_file() and csv_file.stat().st_size > 0:
            size = csv_file.stat().st_size
            try:
                with csv_file.open("rb") as capture:
                    capture.seek(max(0, size - 32768))
                    tail = capture.read()
            except (PermissionError, FileNotFoundError):
                # Windows denies reads until the native writer closes the file.
                return
            if tail:
                footer = next(csv.reader([tail.decode("utf-8", errors="replace").splitlines()[-1]]))
                complete = "[HasHeaderRowAtEnd]" in footer and footer[footer.index("[HasHeaderRowAtEnd]") + 1] == "1"
                if complete and size == finished_size and time.monotonic() - finished_at >= .25:
                    complete_profile()
                elif complete and size != finished_size:
                    finished_size, finished_at = size, time.monotonic()
        return
    game = namespace["editor"].get_game_world()
    if game and not capturing:
        unreal.SystemLibrary.execute_console_command(game, "csv.CompressionMode 0")
        unreal.SystemLibrary.execute_console_command(game, "csvprofile startfile=" + filename)
        unreal.SystemLibrary.execute_console_command(game, "csvprofile start")
        capturing = True
        print("WAREHOUSE_PROFILE_START", label)
    namespace["tick"](delta)
    if pending is None and verify_startup and namespace["report"]["samples"]:
        row = namespace["report"]["samples"][-1]
        if row["t"] in startup_reference and row["t"] not in verified_times:
            expected = startup_reference[row["t"]]
            for actual_vehicle, original_vehicle in zip(row["fleet"], expected["fleet"]):
                if any(actual_vehicle[key] != original_vehicle[key] for key in ("pos", "yaw", "tail")):
                    finish_profile(False, "Initial vehicle pose/route differs from baseline at " + str(row["t"]))
                    return
            verified_times.add(row["t"])
            print("WAREHOUSE_PROFILE_STARTUP_EXACT", row["t"])
    if pending is None and duration is not None and namespace["clock"] >= duration:
        finish_profile(True, "Timed profile; no full-workflow completion claim")


namespace["finish"] = finish_profile
namespace["handle"] = unreal.register_slate_post_tick_callback(tick)
