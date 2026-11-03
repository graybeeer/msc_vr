"""Compare native autonomous travel without changing running bodies or saved maps.

Use -BenchGroup=matrix -StressCases=4,8,10 -SpeedProfiles=6 -HandlingStages=1
-Clearance=1 -LocStages=3 -WorkTopChannels=32 -WorkTopRangeCm=3000
-WorkGraceCm=0 -RollingRun=Before/After. Four flat jobs share two load/seed pairs;
native head-on yield, parked-vehicle detour and no-escape fixtures follow.
"""
from pathlib import Path
import re,unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root=Path(unreal.Paths.project_dir()).resolve()
source=root/'Scripts/verify_work_speed_pie.py'
work=dict(__file__=str(source),__name__='__rolling_steering__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),work)
env=work['env'];diag=work['diag'];bench=work['bench']
command=unreal.SystemLibrary.get_command_line()
match=re.search(r'-RollingRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/AutonomousRollingSteering_20261103'/(match.group(1) if match else 'After')
out.mkdir(parents=True,exist_ok=True)
work['out']=out;diag['out']=out;bench['out']=out
bench['scope']['out']=out;bench['scope']['scope']['OUT']=out
env['output']=out/'NativeReport.json'
traffic=env['CASES']
assert {c['speed_profile'] for c in traffic}=={6},'Use -SpeedProfiles=6'
assert {c['id'] % 1000 for c in traffic}=={4,8,10},'Use -StressCases=4,8,10'
template=traffic[0]
env['CASES']=[dict(template,id=62000+i*10+j,name=f'flat_{mass}kg_seed{seed}',
    kind='parallel',vehicles=1,seed=seed,limit=360,load_kg=mass)
    for i,seed in enumerate((202,203)) for j,mass in enumerate((50,200))]+traffic
