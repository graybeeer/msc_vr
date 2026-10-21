"""Revise the existing Blender source through MCP, preserving the moving rig."""
import ast, math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['Cargo Elevator - model']
bpy.context.window.scene=scene
scene.frame_set(1)
asset=bpy.data.collections['ELEVATOR_ASSET']
studio=bpy.data.collections['ELEVATOR_STUDIO']
groups={}
steel=bpy.data.materials['ELV Brushed stainless']
steel2=bpy.data.materials['ELV Satin door steel']
dark=bpy.data.materials['ELV Graphite frame']
black=bpy.data.materials['ELV Display glass']
white=bpy.data.materials['ELV White lettering']
green=bpy.data.materials['ELV Indicator mint']
yellow=bpy.data.materials['ELV Safety yellow']
red=bpy.data.materials['ELV Emergency red']
font=bpy.data.fonts.load('C:/Windows/Fonts/malgunbd.ttf',check_existing=True)
tree=ast.parse((ROOT/'Scripts/build_cargo_elevator.py').read_text(encoding='utf-8-sig'))
helpers=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('finish','box','cylinder','text','build_call_stand')],type_ignores=[])
exec(compile(helpers,'cargo_elevator_helpers','exec'))
prefixes=('Door pocket cover','Corrugated blue rib','Call panel ','Call button ',
          'Small emergency yellow plate','Emergency stop button','Call stand ','Exposed door travel rail')
for obj in list(asset.objects):
    if obj.name.startswith(prefixes):bpy.data.objects.remove(obj,do_unlink=True)
for floor in range(3):
    group=f'Landing_L{floor+1}'
    box('Exposed door travel rail',(-2.04,0,floor*4+2.82),(.10,5.12,.085),dark,group)
    build_call_stand(floor)
assert sum(o.name.startswith('Call stand control head') for o in asset.objects)==3
assert not any(o.name.startswith(('Door pocket cover','Corrugated blue rib','Call panel ')) for o in asset.objects)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Art/Equipment/CargoElevator/cargo_elevator.blend'))
print('CALL_STANDS_REVISED: 3 independent pedestals; broad wing plates removed')
