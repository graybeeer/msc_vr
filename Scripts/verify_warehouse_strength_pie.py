"""Run via editor -ExecCmds=py; checks real Chaos contacts, exports previews, then quits without saving."""
import unreal, time, traceback, math, os
saved=os.path.abspath(unreal.Paths.project_saved_dir())
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
origin=(600,-1600,215); aim=(-215,-1330,110)
dx,dy,dz=[aim[i]-origin[i] for i in range(3)]
view=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(*origin),unreal.Rotator(yaw=math.degrees(math.atan2(dy,dx)),pitch=math.degrees(math.atan2(dz,math.hypot(dx,dy)))))
view.set_actor_label('StrengthPreview')
capture=view.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1440,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
capture.fov_angle=65
capture.capture_every_frame=False
capture.always_persist_rendering_state=True
started=time.monotonic(); phase=0; phase_time=0; game=None; system=None; named={}; gamecap=None; initial_z=0

def finish():
 unreal.unregister_slate_post_tick_callback(handle)
 level.editor_request_end_play()
 unreal.SystemLibrary.quit_editor()

def tick(dt):
 global phase,phase_time,game,system,named,gamecap,initial_z
 try:
  if time.monotonic()-started>180:
   raise AssertionError('PIE verification timeout')
  if phase==0:
   if time.monotonic()-started<10:return
   level.editor_play_simulate();phase=1;return
  game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
  if not game:return
  now=unreal.GameplayStatics.get_time_seconds(game)
  if phase==1:
   if now<2:return
   named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
   system=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseDamageSystem))
   assert all(s.damage==0 for s in system.get_editor_property('objects')), 'Normal scene damaged during startup'
   gamecap=named['StrengthPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
   gamecap.capture_scene();phase_time=now;phase=2;return
  gamecap.capture_scene()
  if phase==2:
   if now-phase_time<1:return
   unreal.RenderingLibrary.export_render_target(game,gamecap.texture_target,saved,'strength_before.png')
   pallet=named['WH_Pallet_Left_00_1'];initial_z=pallet.get_actor_location().z
   system.apply_impact(named['WH_Rack_Left_00_G'],1800)
   box=named['WH_Cargo_Left_02_1_Top']
   body=box.get_component_by_class(unreal.StaticMeshComponent)
   body.set_simulate_physics(False)
   # Reset and teleport the physical body before starting the drop.
   center,extent=box.get_actor_bounds(False)
   body.set_world_location(body.get_world_location()+unreal.Vector(600,-600,250)-center,False,True)
   body.set_collision_profile_name('PhysicsActor')
   body.set_enable_gravity(True)
   body.set_simulate_physics(True);body.set_physics_linear_velocity(unreal.Vector());body.wake_all_rigid_bodies()
   phase_time=now;phase=3;return
  if phase==3:
   if now-phase_time<8:return
   current=named['WH_Pallet_Left_00_1'].get_actor_location().z
   assert current<initial_z-10, ('Upper pallet did not fall',initial_z,current)
   box=named['WH_Cargo_Left_02_1_Top']
   assert system.has_failed(box), 'Real gravity drop did not generate impact damage'
   unreal.RenderingLibrary.export_render_target(game,gamecap.texture_target,saved,'strength_after.png')
   print('WAREHOUSE_STRENGTH_PIE_VERIFIED actual gravity, hit impulse, falling rack/pallet/cargo, no startup damage',initial_z,current)
   finish()
 except Exception:
  print('STRENGTH_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
