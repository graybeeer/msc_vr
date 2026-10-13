"""Native saved-warehouse editing, FIFO held cargo and absent-facility checks. Never saves."""
import json, time, traceback, unreal
from pathlib import Path

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.WarehouseForklift): actor.set_editor_property('auto_start',False)

output=Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/LayoutInventory');output.mkdir(parents=True,exist_ok=True)
started=time.monotonic();phase='start';at=0;game=live=pc=layout=store=computer=strength=None
placed=None;checks=[];new_vehicle=None

def check(name,**details):
    checks.append(dict(name=name,**details));print('LAYOUT_INVENTORY_CHECK',name,json.dumps(details,ensure_ascii=False))

def finish(ok,detail):
    (output/'Report.json').write_text(json.dumps(dict(ok=ok,detail=detail,checks=checks,wall_seconds=time.monotonic()-started),ensure_ascii=False,indent=2),encoding='utf-8')
    print('LAYOUT_INVENTORY_PASSED' if ok else 'LAYOUT_INVENTORY_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle)
    if layout and layout.get_editor_property('editing'): layout.set_editing(False,0)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def allof(cls): return unreal.GameplayStatics.get_all_actors_of_class(game,cls)
def point(actor):
    p=actor.get_actor_location();return unreal.Vector(p.x,p.y,0)
def surface(actor):
    c,e=actor.get_actor_bounds(False);return unreal.Vector(c.x,c.y,c.z+e.z)

def tick(dt):
    global game,live,pc,layout,store,computer,strength,phase,at,placed,new_vehicle
    try:
        assert time.monotonic()-started<600,('Timeout',phase)
        if phase=='start': level.editor_request_begin_play();phase='settle';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='settle':
            if now<3:return
            live={a.get_actor_label():a for a in allof(unreal.Actor)}
            pc=unreal.GameplayStatics.get_player_controller(game,0);pc.set_observer_floor(0);pc.toggle_layout_editing()
            layout=next(iter(allof(unreal.WarehouseLayoutEditor)));store=next(iter(allof(unreal.WarehouseLayoutInventory)))
            computer=live['WH_ManagementComputer'];strength=live['WH_DamageSystem']
            assert layout.get_editor_property('editing') and unreal.GameplayStatics.is_game_paused(game)
            assert pc.set_layout_action(0) is None
            layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
            first=layout.get_add_choice_label();presets={first}
            for _ in range(500):
                layout.cycle_add_choice();name=layout.get_add_choice_label()
                if name==first:break
                presets.add(name)
            assert {w.get_actor_label() for w in allof(unreal.WarehouseWorker)}<=presets,'Some worker roles lost from the add catalogue'
            check('other_catalogue_keeps_all_worker_role_presets',presets=len(presets))
            layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
            original=live['WH_Drop_Box'];identity=original.get_editor_property('cargo_id');mass=original.get_editor_property('gross_mass_kg')
            inventory_before=len(allof(unreal.WarehouseCargo));layout.set_selection([original]);layout.set_action(unreal.WarehouseLayoutAction.DELETE)
            assert layout.delete_selection(),layout.get_editor_property('status')
            held=store.get_editor_property('held_cargo')
            assert len(held)==1 and held[0].cargo_id==identity and abs(held[0].mass_kg-mass)<.001
            assert len(allof(unreal.WarehouseCargo))==inventory_before-1
            computer.refresh_state();assert not any(i.cargo_id==str(identity) for i in computer.get_editor_property('inventory'))
            check('deletion_removes_physics_and_inventory_preserves_data',cargo_id=str(identity),mass_kg=mass)
            assert computer.create_held_cargo('FIFO-SECOND','재사용 시험 상품',13,unreal.Vector(20,15,10))
            assert not computer.create_held_cargo('FIFO-SECOND','중복',13,unreal.Vector(20,15,10))
            assert not computer.create_held_cargo('INVALID','잘못된 무게',-1,unreal.Vector(20,15,10))
            layout.set_action(unreal.WarehouseLayoutAction.ADD)
            p=unreal.Vector(-750,-1250,0)
            assert layout.begin_add(p),layout.get_editor_property('status')
            layout.update_surface_drag(p,None,p);assert not layout.end_drag(False)
            assert len(store.get_editor_property('held_cargo'))==2 and len(allof(unreal.WarehouseCargo))==inventory_before-1
            check('cancel_keeps_fifo_and_destroys_preview')
            assert layout.begin_add(p);layout.update_surface_drag(p,None,p);assert layout.end_drag(),layout.get_editor_property('status')
            assert len(store.get_editor_property('held_cargo'))==1 and store.get_editor_property('held_cargo')[0].cargo_id=='FIFO-SECOND'
            placed=next(c for c in allof(unreal.WarehouseCargo) if c.get_editor_property('cargo_id')==identity)
            assert placed.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() and abs(placed.get_editor_property('gross_mass_kg')-mass)<.001
            check('first_item_committed_next_item_exposed')
            # The next, smaller parcel must snap onto the first actual parcel.
            top=surface(placed)
            assert layout.begin_add(point(placed));layout.update_surface_drag(point(placed),placed,top)
            assert layout.end_drag(),layout.get_editor_property('status')
            second=next(c for c in allof(unreal.WarehouseCargo) if c.get_editor_property('cargo_id')=='FIFO-SECOND')
            _,dimensions=second.get_actor_bounds(False)
            assert abs(dimensions.x*2-20)<1 and abs(dimensions.y*2-15)<1 and abs(dimensions.z*2-10)<1,('Input size changed',dimensions)
            assert len(store.get_editor_property('held_cargo'))==0 and second.get_actor_bounds(False)[0].z>top.z
            check('box_on_box_snap')
            layout.set_editing(False,0);at=now;phase='physics_settle';return
        if phase=='physics_settle':
            if now-at<2:return
            second=next(c for c in allof(unreal.WarehouseCargo) if c.get_editor_property('cargo_id')=='FIFO-SECOND')
            assert second in strength.get_supported_actors(placed)
            assert second.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() and not second.get_attach_parent_actor()
            assert second.get_actor_bounds(False)[0].z>placed.get_actor_bounds(False)[0].z
            check('stack_keeps_native_physics_and_support')
            layout.set_editing(True,0)
            # Surface snapping onto a real elevated pallet is allowed.
            elevated=[p for p in allof(unreal.WarehousePallet) if 100<p.get_actor_location().z<200]
            assert elevated
            support=elevated[0]
            # Prefer a parcel already on that pallet, so existing cargo is not overlapped.
            supported=[c for c in strength.get_supported_actors(support) if isinstance(c,unreal.WarehouseCargo)]
            target=max(supported,key=lambda c:surface(c).z) if supported else support
            print('RACK_SURFACE_DIAGNOSTIC',target.get_actor_label(),target.get_actor_bounds(False),surface(target))
            assert computer.create_held_cargo('RACK-SNAP','랙 위 적재 시험',4,unreal.Vector(10,10,10))
            layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
            assert layout.begin_add(point(target));layout.update_surface_drag(point(target),target,surface(target))
            assert layout.end_drag(),layout.get_editor_property('status')
            check('parcel_on_rack_pallet_or_its_cargo')
            assert computer.create_held_cargo('PERMANENT-DELETE','데이터 삭제 시험',5,unreal.Vector(20,20,20))
            assert computer.delete_held_cargo('PERMANENT-DELETE') and not store.get_editor_property('held_cargo')
            check('console_create_validate_and_permanent_delete')
            # Restore original mesh rig for a newly added machine, rather than a placeholder.
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            before=allof(unreal.WarehouseForklift)
            p=unreal.Vector(2400,-1850,0)
            assert layout.begin_add(p),layout.get_editor_property('status')
            layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
            new_vehicle=next(v for v in allof(unreal.WarehouseForklift) if v not in before)
            assert abs(new_vehicle.get_physical_mass_kg()-1000)<.5
            assert len(new_vehicle.get_components_by_class(unreal.StaticMeshComponent))>=12
            assert new_vehicle.get_actor_bounds(False)[1].z>100
            check('added_vehicle_preserves_model_and_physical_rig',mass_kg=new_vehicle.get_physical_mass_kg())
            # Delete both chargers; low battery must produce a user-visible code.
            layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
            for station in list(allof(unreal.WarehouseChargingStation)):
                layout.set_selection([station]);assert layout.delete_selection(),layout.get_editor_property('status')
            assert not allof(unreal.WarehouseChargingStation)
            new_vehicle.set_editor_property('battery_percent',29);new_vehicle.toggle_power()
            # Delete the idle elevator and verify queued cross-floor work is rejected.
            layout.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
            lift=allof(unreal.WarehouseElevator)[0];layout.set_selection([lift]);assert layout.delete_selection(),layout.get_editor_property('status')
            assert not allof(unreal.WarehouseElevator)
            job=unreal.WarehouseWorkOrder(job_id='NO-LIFT',pallet=live['WH_TrainingPallet'],destination=unreal.Transform(location=unreal.Vector(1200,0,800)))
            assert not new_vehicle.submit_job(job) and new_vehicle.get_editor_property('last_error_code')=='NO_ELEVATOR'
            check('missing_elevator_rejected_before_dispatch')
            layout.set_category(unreal.WarehouseLayoutCategory.ZONE)
            outbound=next(z for z in layout.get_editor_property('zones') if z.name=='Outbound')
            layout.set_selection([outbound.members[0]]);assert layout.delete_selection(),layout.get_editor_property('status')
            assert not computer.get_editor_property('outbound_destinations')
            assert not computer.issue_outbound(placed,None,0) and 'NO_OUTBOUND_ZONE' in computer.get_editor_property('last_message')
            check('missing_outbound_zone_has_no_default_coordinate_fallback')
            layout.set_editing(False,0);at=now;phase='missing_charger';return
        if phase=='missing_charger':
            if now-at<4:return
            assert new_vehicle.get_editor_property('last_error_code')=='NO_CHARGER',new_vehicle.get_editor_property('status')
            assert '충전할 수 없다' in new_vehicle.get_editor_property('last_error_message')
            assert '충전할 수 없다' in pc.get_editor_property('resource_alert_text')
            check('missing_charger_safely_waits_and_displays_alert')
            layout.set_editing(True,0);layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            if new_vehicle.get_editor_property('powered'):new_vehicle.toggle_power()
            for v in list(allof(unreal.WarehouseForklift)):
                layout.set_selection([v]);assert layout.delete_selection(),layout.get_editor_property('status')
            computer.refresh_state();assert not computer.get_editor_property('fleet')
            check('zero_vehicle_inventory_refresh_is_safe')
            # Restore deleted equipment from captured templates. Test commit,
            # active references and delete rollback, rather than class existence.
            layout.set_action(unreal.WarehouseLayoutAction.ADD)
            p=unreal.Vector(2400,-1650,0)
            for category,cls in ((unreal.WarehouseLayoutCategory.RACK,unreal.StaticMeshActor),
                                 (unreal.WarehouseLayoutCategory.CHARGER,unreal.WarehouseChargingStation),
                                 (unreal.WarehouseLayoutCategory.WAITING_BAY,unreal.WarehouseWaitingBay),
                                 (unreal.WarehouseLayoutCategory.PALLET,unreal.WarehousePallet)):
                layout.set_category(category);before=set(allof(cls))
                assert layout.begin_add(p),layout.get_editor_property('status')
                layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
                created=next(a for a in allof(cls) if a not in before)
                if cls==unreal.WarehousePallet:assert created.root_component.is_simulating_physics()
                layout.set_selection([created]);assert layout.delete_selection(),layout.get_editor_property('status')
                check('facility_add_delete',category=str(category))
            layout.set_category(unreal.WarehouseLayoutCategory.ZONE)
            for _ in range(30):
                if layout.get_add_choice_label()=='출고 대기 구역':break
                layout.cycle_add_choice()
            else:assert False,'Outbound prototype missing'
            p=unreal.Vector(-1000,-1640,0)
            assert layout.begin_add(p);layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
            assert computer.get_editor_property('outbound_destinations')
            assert not computer.issue_outbound(placed,None,0) and 'NO_FORKLIFT' in computer.get_editor_property('last_message')
            check('deleted_outbound_zone_restored_with_destinations')
            outbound=next(z for z in layout.get_editor_property('zones') if z.name=='Outbound')
            p=outbound.centre
            assert layout.begin_drag(outbound.members[0],p)
            layout.update_drag(p+unreal.Vector(10,0,0));assert layout.end_drag(),layout.get_editor_property('status')
            current_destinations=list(computer.get_editor_property('outbound_destinations'))
            layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
            for _ in range(500):
                if layout.get_add_choice_label()=='WH_ManagementComputer':break
                layout.cycle_add_choice()
            else:assert False,'Computer preset missing'
            before=set(allof(unreal.WarehouseComputer));p=unreal.Vector(2400,-1650,0)
            assert layout.begin_add(p);layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
            clone=next(c for c in allof(unreal.WarehouseComputer) if c not in before)
            assert list(clone.get_editor_property('outbound_destinations'))==current_destinations,'New console reused old prototype coordinates'
            layout.set_selection([clone]);assert layout.delete_selection()
            check('new_console_uses_live_rearranged_outbound_destinations')
            layout.set_category(unreal.WarehouseLayoutCategory.ZONE)
            # Recreate the last inbound dispatcher and recover live rack slots.
            inbound=next(z for z in layout.get_editor_property('zones') if z.name=='Inbound')
            layout.set_selection([inbound.members[0]]);assert layout.delete_selection(),layout.get_editor_property('status')
            assert not allof(unreal.WarehouseInboundZone)
            for _ in range(30):
                if layout.get_add_choice_label()=='입고 대기 구역':break
                layout.cycle_add_choice()
            p=unreal.Vector(1100,-1640,0)
            assert layout.begin_add(p);layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
            restored_inbound=allof(unreal.WarehouseInboundZone)[0]
            assert restored_inbound.get_editor_property('rack_slots')
            assert all(s.rack and not s.rack.is_actor_being_destroyed() for s in restored_inbound.get_editor_property('rack_slots'))
            check('last_inbound_dispatcher_restored_with_live_rack_catalogue')
            layout.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
            layout.set_editing(True,2)
            p=unreal.Vector(1000,1600,800)
            assert layout.begin_add(p),layout.get_editor_property('status')
            layout.update_drag(p);assert layout.end_drag(),layout.get_editor_property('status')
            restored=allof(unreal.WarehouseElevator)[0]
            assert [round(restored.floor_height(i)) for i in range(3)]==[0,400,800]
            check('deleted_elevator_restored_with_common_floors')
            finish(True,'FIFO archive/add/cancel; native box stacking incl elevated rack cargo; real rig creation; deleted elevator, charger, outbound zone and all vehicles; no map saved')
    except Exception:
        finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
