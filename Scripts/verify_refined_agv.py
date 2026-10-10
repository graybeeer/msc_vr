"""Check the saved refined rig, dimensions and source body proportions; never saves."""
from pathlib import Path
import unreal
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/FirstPerson/Lvl_FirstPerson')
v=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='WH_AutonomousForklift')
parts={c.get_name():c for c in v.get_components_by_class(unreal.StaticMeshComponent)}
assert len(parts)==12,list(parts)
for name,c in parts.items():
    assert 'SM_Refined_AGV_'+name in c.static_mesh.get_name(),(name,c.static_mesh)
    scale=c.get_editor_property('relative_scale3d')
    assert (scale-unreal.Vector(1,1,1)).length()<.0001,(name,scale)
    assert all(s.material_interface and '/AGV/Materials/' in s.material_interface.get_path_name() for s in c.static_mesh.static_materials),name
# Recorded source FBX bounds, available even when rebuildable raw imports are absent.
s=215/282.55
b=parts['Body'].static_mesh.get_bounds()
for actual,expected in zip((b.box_extent.x*2,b.box_extent.y*2,b.box_extent.z*2),(131*s,120.5*s,279.05*s)):
    assert abs(actual-expected)<.05,(actual,expected)
assert abs(b.origin.z+b.box_extent.z-215)<.05
for name in ('ForkL','ForkR'):
    b=parts[name].static_mesh.get_bounds()
    assert abs(b.origin.x+b.box_extent.x-115)<.05
    assert abs(b.box_extent.y*2-18)<.05
    assert abs(b.origin.z-b.box_extent.z-3.5)<.05
for name,sign in (('LoadWheelL',1),('LoadWheelR',-1)):
    c=parts[name];b=c.static_mesh.get_bounds();loc=c.get_editor_property('relative_location')
    assert abs(loc.x-(-7*s))<.05
    assert abs(loc.y-sign*45*s)<.05, 'Wheel must stay at its original chassis mount'
    assert abs(loc.z-b.box_extent.z)<.05
assert parts['LiftRam'].get_attach_parent()==parts['LiftStage']
assert parts['LiftPulley'].get_attach_parent()==parts['LiftStage']
assert parts['LiftChains'].get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
assert not v.check_systems(),v.check_systems()
print('REFINED_AGV_GEOMETRY_PASSED','uniform body, 215cm height, original wheel mounts, 115cm forks, 12 PBR parts')
exec((Path(unreal.Paths.project_dir())/'Scripts/verify_mezzanine.py').read_text(encoding='utf-8'))
