"""Author architectural building tags. Fixed racks use their existing LayoutType:Rack role at runtime.

configure() edits only tags in the loaded level; -ApplySensorMapTags permits
saving the warehouse. -IsolateStandaloneAgvPrototype additionally removes the
old standalone test vehicle from this operating map, preserving its code/assets.
Rack contents do not inherit a map role. Static mobility alone never grants one.
"""
import json
from pathlib import Path
import re
import unreal

MAP_TAG = 'WarehouseMapStructure'


def is_building(actor):
    if not isinstance(actor, unreal.StaticMeshActor):
        return False
    tags = {str(t) for t in actor.tags}
    if any(t.startswith(('LayoutType:Rack', 'LayoutType:Charger', 'LayoutGroup:TruckDock')) for t in tags):
        return False
    name = actor.get_actor_label()
    return (
        name in {'Floor', 'WH_InboundFloor', 'MZ_LiftPitBase', 'EXT_HeaderCladding'}
        or name.startswith(('WH_Wall_', 'WH_Roof', 'WH_Column_', 'MZ_GroundRear',
                            'EXT_FrontCladding_', 'EXT_SideCladding_',
                            'EXT_SideParapet_', 'EXT_FrontParapet'))
        or re.match(r'^MZ_L[23]_(Deck_|Column_|Beam_|CrossBeam_|Step_|Stair|FacadeBand_)', name) is not None
    )


def configure():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    mapped, removed = [], []
    for actor in actors.get_all_level_actors():
        tags = [str(t) for t in actor.tags]
        wanted = is_building(actor)
        if wanted:
            mapped.append(actor.get_actor_label())
        if wanted != (MAP_TAG in tags):
            actor.modify()
            actor.tags = list(dict.fromkeys(tags + [MAP_TAG])) if wanted else [t for t in tags if t != MAP_TAG]
            if not wanted:
                removed.append(actor.get_actor_label())
    report = {'fixed_building': sorted(mapped), 'removed': sorted(removed), 'count': len(mapped)}
    target = Path(unreal.Paths.project_saved_dir()) / 'Testing/SensorMap/Tags.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SENSOR_BUILDING_MAP_TAGS', len(mapped), 'interior equipment excluded')
    return report


if __name__ == '__main__':
    if '-ApplySensorMapTags' in unreal.SystemLibrary.get_command_line():
        assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/FirstPerson/Lvl_FirstPerson')
        report = configure()
        assert report['count'] > 30, report
        if '-IsolateStandaloneAgvPrototype' in unreal.SystemLibrary.get_command_line():
            actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            prototypes = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.AgvTestVehicle)]
            assert all(a.get_actor_label() == 'AGV_WH_Vehicle' for a in prototypes), 'Review unexpected standalone vehicles before isolation'
            for actor in prototypes:
                print('SENSOR_ISOLATE_STANDALONE_PROTOTYPE', actor.get_actor_label())
                assert actors.destroy_actor(actor)
        assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    else:
        configure()
