"""Verify saved workers and Pawn safety detection; test transforms are never saved."""
import os,sys,unreal
sys.path.insert(0,os.path.dirname(__file__))
from configure_warehouse_workers import WORKERS,PLAYER
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
workers=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseWorker)]
assert len(workers)==len(WORKERS)==7
assert {str(w.get_editor_property('role_name')) for w in workers}=={item[1] for item in WORKERS}
source=unreal.get_default_object(unreal.load_class(None,PLAYER)).get_editor_property('mesh')
for w in workers:
    mesh=w.get_editor_property('mesh')
    assert mesh.get_skeletal_mesh_asset()==source.get_skeletal_mesh_asset(),'Worker mesh differs from player'
    assert all(mesh.get_material(i)==source.get_material(i) for i in range(source.get_num_materials()))
    assert w.get_editor_property('auto_possess_ai')==unreal.AutoPossessAI.DISABLED
    assert not w.get_editor_property('ai_controller_class')
    assert str(w.get_component_by_class(unreal.CapsuleComponent).get_collision_profile_name())=='Pawn'
    assert w.has_standing_clearance(),w.get_actor_label()
    label=w.get_component_by_class(unreal.WidgetComponent)
    assert label.get_editor_property('space')==unreal.WidgetSpace.SCREEN
    assert label.get_editor_property('relative_location').z>w.get_component_by_class(unreal.CapsuleComponent).get_unscaled_capsule_half_height()
print('WORKERS_SAVED_VERIFIED seven roles, player visuals, no AI, collision clearance, overhead UI')

from configure_forklift_autonomy import configure_autonomy
v=configure_autonomy(True)
v.toggle_power()
S=unreal.WarehouseAIState
for _ in range(3000):
    v.advance_simulation(.05)
    assert v.get_editor_property('ai_state')!=S.FAULT,v.get_editor_property('status')
    if v.get_editor_property('ai_state')==S.INSERT_FORK:break
else:raise AssertionError('Forklift did not reach insertion')
worker=workers[0]
original=worker.get_actor_transform()
worker.set_actor_location(unreal.Vector(1000,-710,96.5),False,True)
position=v.get_actor_location()
for _ in range(100):
    v.advance_simulation(.05)
    if v.get_editor_property('ai_state')==S.WAITING_OBSTACLE:break
assert v.get_editor_property('ai_state')==S.WAITING_OBSTACLE
assert (v.get_actor_location()-position).length()<.1,'Vehicle moved through worker'
worker.set_actor_transform(original,False,True)
for _ in range(3000):
    v.advance_simulation(.05)
    assert v.get_editor_property('ai_state')!=S.FAULT,v.get_editor_property('status')
    if 'DEMO-001' in v.get_editor_property('completed_job_ids'):break
else:raise AssertionError('Vehicle failed to resume after worker cleared')
print('WORKERS_SAFETY_VERIFIED worker stops AGV; work resumes after clearance')
