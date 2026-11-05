"""Same mapped native transport from four pallet headings; never forces runtime motion."""
from pathlib import Path
import math,re,unreal
source=Path(__file__).with_name('verify_pickup_reposition_pie.py')
scope=dict(__file__=str(source),__name__='__four_way_transport__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),scope)
env=scope['env'];work=scope['work']
run=re.search(r'-FourWayRun=([A-Za-z0-9_-]+)',unreal.SystemLibrary.get_command_line())
out=Path(unreal.Paths.project_saved_dir())/'Testing/PalletFourWay_20261104'/(run.group(1) if run else 'Transport')
out.mkdir(parents=True,exist_ok=True)
scope['out']=out
work['out']=out;work['diag']['out']=out;work['bench']['out']=out
work['bench']['scope']['out']=out;work['bench']['scope']['scope']['OUT']=out
env['output']=out/'NativeReport.json'
base=next(c for c in env['CASES'] if c.get('load_kg')==50)
env['CASES']=[dict(base,id=base['id']+i*10000,name='entry_'+str(yaw),scenario='entry_'+str(yaw),entry_yaw=yaw,expect='transport')
              for i,yaw in enumerate((0,90,180,270))]
old_setup=env['setup']
def setup(definition):
    record=old_setup(definition)
    lookup={a.get_actor_label():a for a in env['actors'].get_all_level_actors()}
    job=env['jobs'][0];p=lookup[job['pallet']];centre=p.get_actor_location()
    angle=math.radians(definition['entry_yaw'])
    for name in [job['pallet']]+job['cargo']:
        a=lookup[name];delta=a.get_actor_location()-centre;r=a.get_actor_rotation();r.yaw+=definition['entry_yaw']
        a.set_actor_location(centre+unreal.Vector(delta.x*math.cos(angle)-delta.y*math.sin(angle),delta.x*math.sin(angle)+delta.y*math.cos(angle),delta.z),False,True)
        a.set_actor_rotation(r,True)
    return record
env['setup']=setup
old_complete=env['complete_case']
def complete(ok,detail):
    definition=env['CASES'][env['index']]
    if ok and definition['entry_yaw'] in (90,270):
        vehicle=env['live'][env['jobs'][0]['vehicle']]
        if vehicle.get_editor_property('pickup_entry_quarter_turns') not in (1,3):
            ok=False;detail+='; the vehicle did not exercise a side entry'
    old_complete(ok,detail)
env['complete_case']=complete
