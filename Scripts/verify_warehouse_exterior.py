"""Verify dock continuity and clearances for a future 12 x 3 x 4 m truck."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
named={a.get_actor_label():a for a in actors}
for label in ('EXT_Yard','EXT_AccessRoad','EXT_DockPlatform','EXT_PedestrianRamp','EXT_TruckEntryTarget','EXT_TruckDockTarget_01','EXT_TruckDockTarget_02','WH_AutonomousForklift','WH_TrainingPallet'):
    assert label in named,label
def bounds(actor):
    c,e=actor.get_actor_bounds(False)
    return (c.x-e.x,c.y-e.y,c.z-e.z),(c.x+e.x,c.y+e.y,c.z+e.z)
assert abs(bounds(named['EXT_Yard'])[1][2]+120)<.01
assert abs(bounds(named['EXT_DockPlatform'])[1][2])<.01
assert abs(bounds(named['Floor'])[1][2])<.01
ramp=named['EXT_PedestrianRamp']
transform=ramp.get_actor_transform()
south=transform.transform_location(unreal.Vector(-50,0,50))
north=transform.transform_location(unreal.Vector(50,0,50))
assert abs(south.z+120)<.01 and abs(north.z)<.01,(south,north)
assert abs(south.y+3900)<2 and abs(north.y+2300)<2
for index in (1,2):
    assert named[f'EXT_TruckDockTarget_0{index}'].get_actor_forward_vector().y<-.99

def clear(low,high,label,ignore=None):
    for actor in actors:
        if actor.get_actor_label()==ignore:
            continue
        if not isinstance(actor,unreal.StaticMeshActor):
            continue
        if actor.static_mesh_component.get_collision_profile_name() not in ('BlockAll','BlockAllDynamic'):
            continue
        a,b=bounds(actor)
        assert not all(a[i]<high[i]-.1 and b[i]>low[i]+.1 for i in range(3)), (label,actor.get_actor_label(),a,b)
# Straight entry and docking envelopes. These are clearance checks, not a steering simulation.
clear((1850,-7550,-110),(2150,-3950,280),'truck entry')
clear((-500,-3720,-110),(-200,-2520,280),'empty dock 01')
clear((200,-3720,-110),(500,-2520,280),'occupied dock 02',ignore='EXT_ParkedTruck_02')
truck=named['EXT_ParkedTruck_02']
low,high=bounds(truck)
assert low[0]>160 and high[0]<540 and low[1]>-3930 and abs(high[1]+2535)<.1,(low,high)
assert abs(low[2]+120)<.1,(low,high)
assert truck.get_actor_forward_vector().y<-.99
assert abs(truck.get_actor_scale3d().x-1)<.001
clear(tuple(v+.5 for v in low),tuple(v-.5 for v in high),'parked truck overlap',ignore='EXT_ParkedTruck_02')
mesh_editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for prefix,count in [('EXT_ParkedTruck_',1),('EXT_Beech_',5),('EXT_Shrub_',15)]:
    placed=[a for a in actors if a.get_actor_label().startswith(prefix)]
    assert len(placed)==count,(prefix,len(placed))
    for actor in placed:
        component=actor.static_mesh_component
        mesh=component.get_editor_property('static_mesh')
        assert component.get_collision_profile_name()=='BlockAll'
        assert mesh_editor.get_collision_complexity(mesh)==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        assert all(component.get_material(i) for i in range(component.get_num_materials()))
        if prefix!='EXT_ParkedTruck_':
            assert component.get_editor_property('disallow_nanite')
            assert not component.get_editor_property('can_ever_affect_navigation')
            settings=mesh_editor.get_nanite_settings(mesh)
            assert settings.generate_fallback==unreal.NaniteGenerateFallback.ENABLED
            assert settings.fallback_percent_triangles==(1.0 if prefix=='EXT_Beech_' else .25)
            a,b=bounds(actor)
            assert b[0]<-3020 or a[0]>3020,(actor.get_actor_label(),a,b)
assert not any(a.get_actor_label().startswith(('EXT_TreeTrunk_','EXT_TreeCrown_')) for a in actors)
clear((-150,-2200,5),(150,-1800,185),'warehouse doorway')
assert len([a for a in actors if isinstance(a,unreal.WarehouseCargo)])==262
assert not named['WH_AutonomousForklift'].get_editor_property('powered')
print('WAREHOUSE_EXTERIOR_VERIFIED','Fab truck/foliage, collisions, clear entry/dock 01, ramp and original interior preserved')
