"""Render the cargo lift model through Blender MCP, restoring asset visibility."""
from pathlib import Path
import bpy
from mathutils import Vector

out = Path(__file__).resolve().parents[1] / 'Art/Equipment/CargoElevator'
scene = bpy.data.scenes['Cargo Elevator - model']
bpy.context.window.scene = scene
camera = scene.camera
saved = {o: o.hide_render for o in scene.objects}
saved_camera = camera.matrix_world.copy()
saved_scale = camera.data.ortho_scale
saved_resolution = (scene.render.resolution_x, scene.render.resolution_y)
try:
    camera.location = (-8.4, -6.8, 3.9)
    camera.rotation_euler = (Vector((-1.10, 0, 1.60)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = 7.1
    scene.render.resolution_x, scene.render.resolution_y = 1500, 1250
    for obj in bpy.data.collections['ELEVATOR_ASSET'].objects:
        group = obj.get('elevator_export_group', '')
        if group in ('ShaftFrame', 'GuideRails', 'ShaftEnclosure') or group.endswith(('L2', 'L3')) or group.startswith(('Gate_1_', 'Gate_2_')):
            obj.hide_render = True
    for frame, name in ((1, 'elevator_closed'), (70, 'elevator_open')):
        scene.frame_set(frame)
        scene.render.filepath = str(out / (name + '.png'))
        bpy.ops.render.render(write_still=True)
    for obj, hidden in saved.items():
        obj.hide_render = hidden
    # Remove one side of the enclosure only for the explanatory tower view.
    for obj in bpy.data.collections['ELEVATOR_ASSET'].objects:
        if obj.get('elevator_export_group') == 'ShaftEnclosure' and obj.location.y < -1:
            obj.hide_render = True
    scene.frame_set(230)
    camera.location = (-16, -13, 11)
    camera.rotation_euler = (Vector((0, 0, 5.7)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = 14.2
    scene.render.resolution_x, scene.render.resolution_y = 1200, 1700
    scene.render.filepath = str(out / 'elevator_three_floors.png')
    bpy.ops.render.render(write_still=True)
finally:
    for obj, hidden in saved.items():
        obj.hide_render = hidden
    scene.frame_set(1)
    camera.matrix_world = saved_camera
    camera.data.ortho_scale = saved_scale
    scene.render.resolution_x, scene.render.resolution_y = saved_resolution
print('ELEVATOR_RENDERS_COMPLETE')
