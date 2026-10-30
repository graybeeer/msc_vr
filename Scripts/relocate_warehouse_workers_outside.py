"""Apply the current outside-worker placement to the saved warehouse only.

The role presenters retain identity, appearance and physical collision. Does not
move the player or visual truck drivers. The root runner owns editor/save access.
"""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world(), 'Stop PIE before saving worker placement'
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
world = editor.get_editor_world()
workers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseWorker)
assert workers, 'No warehouse role presenters found'
for worker in workers:
    worker.modify()
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseLayoutEditor):
    actor.modify()
moved = unreal.WarehouseWorker.relocate_indoor_workers(world)
assert all(worker.has_standing_clearance() for worker in workers), 'A worker has no clear standing space'
assert level.save_current_level()
print('WORKERS_MOVED_OUTSIDE_SAVED', moved, len(workers))
unreal.SystemLibrary.quit_editor()
