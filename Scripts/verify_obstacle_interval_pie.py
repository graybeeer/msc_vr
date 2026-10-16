"""Current cadence regression: autonomous scans at 1s, manual operation never scans.

Compatibility entry point. The old remote-scanner test is superseded by the
manual-operation contract in AUTONOMY.md (2026-10-16).
"""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name('verify_remote_drive_continuity_pie.py')), run_name='__main__')