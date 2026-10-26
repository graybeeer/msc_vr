"""Unsaved 4-floor native FMS transport and idle 4->3->4 guide regression.

Reuses the saved-rig route/physics fixture instead of duplicating its controller.
Only fixture authoring before PIE and public requests occur; no body pose,
velocity, attachment, AI state or completion result is assigned during play.
"""
from pathlib import Path
import json
import runpy
import unreal

request_path = Path(unreal.Paths.project_saved_dir()) / 'Testing/InboundWrapRequest.json'
request = json.loads(request_path.read_text(encoding='utf-8-sig')) if request_path.exists() else {}
runpy.run_path(str(Path(__file__).with_name('verify_elevator_route_visuals_pie.py')),
              init_globals={'BUILDING_FOUR_FLOOR_CHECKS': True, 'BUILDING_GUIDES_ONLY': request.get('mode') in ('guides', 'guides_diag'),
                            'BUILDING_GUIDES_DIAGNOSTIC': request.get('mode') == 'guides_diag'})
