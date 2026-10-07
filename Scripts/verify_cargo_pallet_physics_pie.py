"""Gameplay stress checks for intact cartons, safe releases, dynamic pallets and loaded AGV transport."""
import unreal,time,traceback,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
mesh=unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')
box=actors.spawn_actor_from_class(unreal.WarehouseCargo,unreal.Vector(1400,-900,100));box.set_actor_label('PhysicsTestCarton')
box.set_cargo_mesh(mesh);fit(box,(40,30,25),(1400,-800,12.8));box.set_gross_mass_kg(12)
pallet=actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(1400,-550,.3));pallet.set_actor_label('PhysicsTestPallet')
strength=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_DamageSystem')
specs=list(strength.get_editor_property('objects'))
for a,mass,rated in ((box,12,120),(pallet,25,1500)):
    specs.append(unreal.WarehouseStrength(name=a.get_actor_label(),members=[a],physics_meshes=[a.get_component_by_class(unreal.StaticMeshComponent).static_mesh],mass_kg=mass,rated_load_kg=rated,impact_yield_j=1000,impact_failure_j=5000))
strength.set_editor_property('objects',specs)
preview=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(1300,1250,130),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(1300,1250,130),unreal.Vector(1400,1400,25)))
preview.set_actor_label('PhysicsPreview')
cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
cap.texture_target=unreal.RenderingLibrary.create_render_target2d(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),1000,700,unreal.TextureRenderTargetFormat.RTF_RGBA8)
cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR;cap.fov_angle=65
started=time.monotonic();phase=0;at=0;cycle=0;max_speed=0;initial={};pallet_start=None
def finish():
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
    global phase,at,cycle,max_speed,initial,pallet_start
    try:
        assert time.monotonic()-started<600,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        pc=unreal.GameplayStatics.get_player_controller(game,0);pawn=pc.get_controlled_pawn()
        box=named['PhysicsTestCarton'];pallet=named['PhysicsTestPallet'];body=box.get_component_by_class(unreal.StaticMeshComponent)
        if phase==1:
            if now<.3:return
            cargo=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
            assert all(a.get_component_by_class(unreal.StaticMeshComponent).static_mesh.get_name().startswith('SM_Carton_Intact_') for a in cargo)
            initial={a:a.get_actor_bounds(False)[0] for a in cargo if a!=box}
            phase=2;return
        if phase==2:
            if now<6:return
            unstable=[a.get_actor_label() for a,pos in initial.items() if (a.get_actor_bounds(False)[0]-pos).length()>8]
            shot=named['PhysicsPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            unreal.RenderingLibrary.export_render_target(game,shot.texture_target,'C:/msc_UnrealProject/msc_vr/Saved','IntactCargoPalletPreview.png')
            assert not unstable,('Unstable initial cartons',unstable)
            pbody=pallet.get_component_by_class(unreal.StaticMeshComponent)
            assert pbody.is_simulating_physics() and pallet.root_component==pbody
            assert abs(pbody.get_mass()-25)<.1
            print('CARGO_PALLET_STARTUP_PASSED',len(initial),'stable intact cartons')
            shot.set_editor_property('capture_every_frame',False)
            pawn.set_actor_location(unreal.Vector(1400,-900,96),False,True);pc.set_control_rotation(unreal.Rotator(yaw=90))
            assert pawn.try_pickup_cargo(box)
            at=now;phase=3;return
        if phase==3:
            if now-at<.7:return
            pawn.drop_cargo();assert not box.get_attach_parent_actor(),'No clear release'
            at=now;max_speed=0;phase=4;return
        if phase==4:
            max_speed=max(max_speed,body.get_physics_linear_velocity().length())
            c,e=box.get_actor_bounds(False)
            assert c.z-e.z>-.6,('Carton passed floor',c,e)
            assert max_speed<800,('Release exploded',max_speed)
            if now-at<2:return
            assert abs(c.z-e.z)<2,('Not resting on floor',c,e)
            cycle+=1
            if cycle<10:
                assert pawn.try_pickup_cargo(box),cycle
                at=now;phase=3;return
            print('SAFE_CARGO_RELEASE_PASSED',cycle,'pick/drop cycles')
            body.set_physics_linear_velocity(unreal.Vector())
            box.set_actor_location(box.get_actor_location()+unreal.Vector(0,0,200),False,True)
            body.set_physics_linear_velocity(unreal.Vector(0,0,-2000));body.wake_all_rigid_bodies();at=now;phase=5;return
        if phase==5:
            c,e=box.get_actor_bounds(False);assert c.z-e.z>-.8,('Fast drop tunnelled',c,e)
            if now-at<2:return
            assert abs(c.z-e.z)<2,('Fast fall did not land on ground',c,e,body.get_physics_linear_velocity())
            print('FAST_CARGO_FALL_CCD_PASSED')
            pawn.set_actor_location(unreal.Vector(1400,-700,96),False,True);pc.set_control_rotation(unreal.Rotator(yaw=90))
            pallet.set_editor_property('payload_mass_kg',1)
            assert not pawn.try_pickup_pallet(pallet),'Loaded pallet hand pickup allowed'
            pallet.set_editor_property('payload_mass_kg',0)
            assert pawn.try_pickup_pallet(pallet)
            at=now;phase=6;return
        if phase==6:
            if now-at<.7:return
            pawn.drop_cargo();assert not pallet.get_attach_parent_actor()
            at=now;phase=7;return
        if phase==7:
            c,e=pallet.get_actor_bounds(False);assert c.z-e.z>-.8,('Pallet fell through floor',c,e)
            if now-at<3:return
            pbody=pallet.get_component_by_class(unreal.StaticMeshComponent)
            assert pbody.is_simulating_physics() and abs(pbody.get_mass()-25)<.1
            pallet_start=pallet.get_actor_location()
            pbody.add_impulse(unreal.Vector(5000,0,0),unreal.Name('None'),False)
            at=now;phase=8;return
        if phase==8:
            if now-at<1:return
            assert (pallet.get_actor_location()-pallet_start).length()>2,'Physics body moved without actor pose'
            print('PALLET_CARRY_RELEASE_PUSH_PASSED actor root and physics pose agree')
            pawn.set_actor_location(unreal.Vector(-1800,-1500,96),False,True)
            # Place a supported physical load in a clear aisle. No test state is saved.
            pallet.set_actor_location(unreal.Vector(1400,-1000,.1),False,True)
            pallet.set_actor_rotation(unreal.Rotator(),True)
            pbody=pallet.get_component_by_class(unreal.StaticMeshComponent)
            pbody.set_physics_linear_velocity(unreal.Vector());pbody.set_physics_angular_velocity_in_degrees(unreal.Vector());pbody.wake_all_rigid_bodies()
            box.set_actor_location(unreal.Vector(1400,-1000,27.6),False,True)
            box.set_actor_rotation(unreal.Rotator(),True)
            body.set_physics_linear_velocity(unreal.Vector());body.set_physics_angular_velocity_in_degrees(unreal.Vector());body.wake_all_rigid_bodies()
            v=named['WH_AutonomousForklift']
            v.set_actor_location(unreal.Vector(1180,-1000,0),False,True);v.set_actor_rotation(unreal.Rotator(),True)
            v.set_editor_property('pending_jobs',[unreal.WarehouseWorkOrder(job_id='PHYSICS-LOAD-SLIP',source_system='PHYSICS-TEST',pallet=pallet,destination=unreal.Transform(location=unreal.Vector(1400,-700,0)))])
            v.toggle_power();at=now;phase=9;return
        if phase==9:
            assert now-at<90,'Physical pickup did not complete'
            v=named['WH_AutonomousForklift']
            assert body.is_simulating_physics() and pallet.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(),'Fork contact disabled load physics'
            assert not box.get_attach_parent_actor() and not pallet.get_attach_parent_actor(),'Load rigidly attached to forklift'
            assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,(v.get_editor_property('status'),pallet.get_actor_location(),pallet.get_actor_rotation())
            if v.get_editor_property('ai_state')==unreal.WarehouseAIState.TRAVEL_HEIGHT:
                assert pallet.get_actor_location().z>5,('Fork did not physically lift pallet',pallet.get_actor_location())
                assert pallet.get_actor_location().x<1350,('Pallet did not travel on physical fork contact',pallet.get_actor_location())
                print('DYNAMIC_FORK_CONTACT_PASSED lift and reverse travel with pallet and carton physics continuously enabled')
                body.add_impulse(unreal.Vector(0,5000,0),unreal.Name('None'),False)
                at=now;phase=10
        if phase==10:
            v=named['WH_AutonomousForklift']
            assert body.is_simulating_physics() and pallet.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            c,e=box.get_actor_bounds(False);assert c.z-e.z>-.8,('Slipped cargo passed through floor',c,e)
            if v.get_editor_property('ai_state')==unreal.WarehouseAIState.FAULT:
                assert 'SUPPORT LOST' in v.get_editor_property('status'),v.get_editor_property('status')
                print('PHYSICAL_LOAD_SLIP_FAULT_PASSED',v.get_editor_property('status'))
                print('CARGO_PALLET_PHYSICS_PIE_PASSED intact models, stable stacks, ten safe drops, CCD, dynamic carryable pallet, physical fork lift/travel, load slip exception')
                finish()
            else:assert now-at<12,'Load slip was not reported'
    except Exception:
        print('CARGO_PALLET_PHYSICS_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
