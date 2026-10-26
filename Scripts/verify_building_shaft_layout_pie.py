"""Building geometry and both lift kinds share reversible native floor domains.

Runs the existing unsaved construction fixture with real native lifts, then
checks 4-floor relocation/deletion and old-hole restoration. No map is saved.
"""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).with_name('verify_construction_editor_pie.py')),
              init_globals={'BUILDING_SHAFT_LAYOUT_CHECKS': True})
