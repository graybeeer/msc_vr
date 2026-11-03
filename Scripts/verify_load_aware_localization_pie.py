"""Load-aware localization A/B on identical real mapped transports.

Use -BenchGroup=flat -WorkLoads=50,200,500 -BenchRepeats=2 -SpeedProfiles=6
-WorkTopChannels=32 -WorkTopRangeCm=3000 -WorkGraceCm=0 -HandlingStages=1
-Clearance=1 -LocStages=3 -LoadAwareModes=0,1 -LoadRun=name.
Only the localization load model changes. No running body, sensor, job result,
load capacity, drive force or collision is assigned by this observer.
"""
from pathlib import Path
import csv
import json
import math
import re
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root = Path(unreal.Paths.project_dir()).resolve()
command = unreal.SystemLibrary.get_command_line()
source = root / 'Scripts/verify_work_speed_pie.py'
work = dict(__file__=str(source), __name__='__load_aware__')
exec(compile(source.read_text(encoding='utf-8-sig'), str(source), 'exec'), work)
env = work['env']
match = re.search(r'-LoadAwareModes=([01,]+)', command, re.I)
modes = [int(n) for n in match.group(1).split(',')] if match else [0, 1]
match = re.search(r'-LoadRun=([A-Za-z0-9_-]+)', command, re.I)
out = root / 'Saved/Testing/LoadAwareLocalization_20261103' / (match.group(1) if match else 'Compare')
out.mkdir(parents=True, exist_ok=True)
work['out'] = out
work['diag']['out'] = out
work['bench']['out'] = out
work['bench']['scope']['out'] = out
work['bench']['scope']['scope']['OUT'] = out
env['output'] = out / 'NativeReport.json'
env['CASES'] = [dict(c, id=c['id'] + mode * 1000000, load_aware=mode,
                     name=c['name'] + '_loadaware' + str(mode)) for mode in modes for c in env['CASES']]
old_setup, old_complete, old_finish = env['setup'], env['complete_case'], env['finish']
rows = []
row = None
last_time = -1
samples = []
errors = []
with (out / 'LoadedMotion.csv').open('w', encoding='utf-8', newline='') as f:
    csv.writer(f).writerow(['case', 't', 'error_cm', 'heading_deg', 'sampled_load_kg', 'scale', 'state'])


def setup(definition):
    global row, last_time, samples, errors
    unreal.SystemLibrary.execute_console_command(env['editor'].get_editor_world(), 'Warehouse.Localization.LoadAware ' + str(definition['load_aware']))
    assert unreal.SystemLibrary.get_console_variable_int_value('Warehouse.Localization.LoadAware') == definition['load_aware']
    result = old_setup(definition)
    row = dict(name=definition['name'], mode=definition['load_aware'], load_kg=definition.get('load_kg'),
               seed=definition['seed'], sampled_load_max_kg=0, scale_max=1, monitor_errors=[])
    last_time, samples, errors = -1, [], []
    return result


def observe(dt):
    global last_time
    if not row or not env.get('case') or not env.get('live'):
        return
    if work['bench']['scope']['scope'].get('in_tick'):
        return
    try:
        game = env['editor'].get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if now == last_time:
            return
        last_time = now
        vehicle = env['live'][env['jobs'][0]['vehicle']]
        loc = vehicle.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
        assert loc
        kg, scale = loc.sampled_load_kg, loc.load_uncertainty_scale
        maximum = math.sqrt(1 + max(0, min(1.4, kg / vehicle.get_editor_property('vehicle_mass_kg')))) if row['mode'] else 1
        assert math.isfinite(scale) and 1 - 1e-5 <= scale <= maximum + 1e-5, (kg, scale, maximum)
        if not row['mode'] or kg == 0:
            assert abs(scale - 1) < 1e-5, (kg, scale)
        row['sampled_load_max_kg'] = max(row['sampled_load_max_kg'], kg)
        row['scale_max'] = max(row['scale_max'], scale)
        if vehicle.get_supported_pallet():
            errors.append(loc.error_cm)
            samples.append([row['name'], now, loc.error_cm, loc.heading_error_deg, kg, scale, str(vehicle.ai_state)])
    except Exception:
        detail = traceback.format_exc()
        if detail not in row['monitor_errors']:
            row['monitor_errors'].append(detail)


def complete(ok, detail):
    if row['monitor_errors'] or not errors or abs(row['sampled_load_max_kg'] - row['load_kg']) > .25:
        ok = False
        detail += '; load model/loaded localization not verified: ' + str(row)
    row.update(loaded_frames=len(errors), loaded_mean_cm=sum(errors) / len(errors) if errors else None,
               loaded_p95_cm=sorted(errors)[math.ceil(len(errors) * .95) - 1] if errors else None,
               loaded_max_cm=max(errors) if errors else None)
    with (out / 'LoadedMotion.csv').open('a', encoding='utf-8', newline='') as f:
        csv.writer(f).writerows(samples)
    old_complete(ok, detail)
    result = env['results'][-1]
    row.update(passed=result['ok'], detail=result['detail'],
               jobs=[dict(duration_s=j['completed'] - j['submitted'] if j.get('completed') is not None else None) for j in env['jobs']])
    rows.append(row.copy())
    (out / 'LoadComparison.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    old_finish()


env.update(setup=setup, complete_case=complete, finish=finish)
handle = unreal.register_slate_post_tick_callback(observe)
