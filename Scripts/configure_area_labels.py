"""Add camera-facing Korean area UI only; preserve the warehouse layout and physics."""
import unreal
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from warehouse_staging_layout import INBOUND,OUTBOUND

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
PREFIX='WH_AreaUI_'
# World centimetres. Anchors sit above their actual work area, below the next floor.
AREAS=[
    ('Inbound','입고 대기 구역',0,(*INBOUND[:2],270)),
    ('Outbound','출고 대기 구역',0,(*OUTBOUND[:2],270)),
    ('Putaway','팔레트 보관 랙',0,(2620,-300,285)),
    ('Receiving','입고·검수 구역',0,(1530,-1120,270)),
    # Keep the legacy key so saved labels and layout groups retain their identity.
    ('Picking','보관·보충 구역',0,(-1040,-710,260)),
    ('Packing','포장·출고 준비',0,(-1100,-1120,270)),
    ('Loading','트럭 상하차 구역',0,(0,-2330,310)),
    ('Control','운영·관제 구역',0,(-1450,1870,290)),
    ('Inventory','재고 관리 구역',0,(-1080,870,260)),
    ('Service','충전·정비 구역',0,(2250,-550,260)),
    ('Pallets','빈 팔레트 보관',0,(1750,1710,230)),
    ('EmptyRacks','빈 랙 보관',0,(-1590,380,285)),
    ('Storage','1층 랙 보관',0,(0,-250,285)),
    ('Lift_L1','1층 · 범용 엘리베이터',0,(1000,1400,320)),
    ('Stairs_L1','1층 · 계단',0,(-1840,-1240,220)),
]
for floor in (1,2):
    z=floor*400
    AREAS.extend([
        (f'StorageWest_L{floor+1}',f'{floor+1}층 · 서측 랙 보관',floor,(-1550,-140,z+285)),
        (f'StorageEast_L{floor+1}',f'{floor+1}층 · 동측 랙 보관',floor,(1550,-140,z+285)),
        (f'Lift_L{floor+1}',f'{floor+1}층 · 범용 엘리베이터',floor,(1000,1400,z+320)),
        (f'Stairs_L{floor+1}',f'{floor+1}층 · 계단',floor,(-1840,-410,z+220)),
    ])

def configure_area_labels():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    if PREFIX+'Buffer' in named:actors.destroy_actor(named[PREFIX+'Buffer'])
    for key,title,floor,position in AREAS:
        label=PREFIX+key
        actor=named.get(label) or actors.spawn_actor_from_class(unreal.WarehouseAreaLabel,unreal.Vector(*position))
        assert isinstance(actor,unreal.WarehouseAreaLabel),label
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(f'Warehouse/AreaUI/L{floor+1}')
        actor.set_editor_property('is_spatially_loaded',False)
        actor.set_editor_property('area_name',unreal.Text(title))
        actor.set_editor_property('floor_index',floor)
        actor.tags=[unreal.Name('WarehouseAreaLabel'),unreal.Name(f'WarehouseFloor{floor}')]
        actor.set_actor_location(unreal.Vector(*position),False,False)
        actor.set_actor_enable_collision(False)
    print('AREA_UI_CONFIGURED',len(AREAS))

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_area_labels()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('AREA_UI_SAVED',len(AREAS))
    unreal.SystemLibrary.quit_editor()
