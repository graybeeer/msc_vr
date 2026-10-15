"""Denser services with explicit, measured wall/ceiling mounts; editor only."""
import unreal,json,math,time,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
# Reuse the validated import/material phase without running the old placement.
code=(ROOT/'Scripts/apply_warehouse_services.py').read_text(encoding='utf-8')
exec(code[:code.index('existing=actors.get_all_level_actors()')])
OUT=ROOT/'Saved/Testing/WarehouseServicesDense';OUT.mkdir(parents=True,exist_ok=True)
existing=actors.get_all_level_actors();before={a.get_actor_label():a.get_actor_transform() for a in existing if not a.get_actor_label().startswith('WS_')}
for a in existing:
    if a.get_actor_label().startswith('WS_'):actors.destroy_actor(a)
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
surfaces=[]
for n,a in named.items():
    if n=='WH_Roof' or 'Deck_' in n or 'CrossBeam_' in n or n.startswith('WH_RoofBeam_'):
        c,e=a.get_actor_bounds(False);surfaces.append((a,c,e))
ec,ee=named['MZ_AGVElevator'].get_actor_bounds(False)
cube=unreal.load_asset('/Engine/BasicShapes/Cube');cylinder=unreal.load_asset('/Engine/BasicShapes/Cylinder')
silver=materials[norm('WS Fasteners')];zinc=materials[norm('WS Galvanized steel')];cablemat=materials[norm('WS Cable black')]
created=[];counts={};mounts=[];supports={};excluded=0
def ceiling(x,y,above):
    candidates=[(c.z-e.z,a) for a,c,e in surfaces if abs(x-c.x)<=e.x+.1 and abs(y-c.y)<=e.y+.1 and c.z-e.z>=above]
    assert candidates,('No supporting ceiling',x,y,above)
    return min(candidates,key=lambda t:t[0])
def tag(a,f,roof,detail=False):
    a.set_editor_property('is_spatially_loaded',False);a.set_folder_path('Warehouse/Services/'+('Roof' if roof else 'Floor'+str(f+1)))
    a.tags=[unreal.Name('WarehouseServices'),unreal.Name('LayoutFixed'),unreal.Name('WarehouseFloor'+str(f))]+([unreal.Name('ObserverRoof')] if roof else [])
    c=a.static_mesh_component;c.set_collision_profile_name('NoCollision');c.set_editor_property('mobility',unreal.ComponentMobility.STATIC);c.set_editor_property('cast_shadow',not detail);c.set_editor_property('ld_max_draw_distance',7000 if detail else 15000)
def spawn(key,pos,yaw=0,scale=(1,1,1),f=0,roof=False,pitch=0,detail=False,parent=None):
    global excluded
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*pos),unreal.Rotator(pitch=pitch,yaw=yaw));a.set_actor_label('WS_'+key+'_'+str(counts.get(key,0)));a.static_mesh_component.set_static_mesh(meshes[key]);a.set_actor_scale3d(unreal.Vector(*scale));tag(a,f,roof,detail)
    c,e=a.get_actor_bounds(False)
    if abs(c.x-ec.x)<e.x+ee.x+30 and abs(c.y-ec.y)<e.y+ee.y+30 and c.z-e.z<ec.z+ee.z+50:
        actors.destroy_actor(a);excluded+=1;return None
    if parent:a.attach_to_actor(parent,unreal.Name('None'),unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
    counts[key]=counts.get(key,0)+1;created.append(a);return a
def primitive(name,loc,size,mesh,mat,f,roof,rotation=None):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*loc),rotation or unreal.Rotator());a.set_actor_label('WS_'+name+'_'+str(len(created)));a.static_mesh_component.set_static_mesh(mesh);a.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)));a.static_mesh_component.set_material(0,mat);tag(a,f,roof,True);created.append(a);supports.setdefault((f,roof),[]).append(a);return a
def rod(a,b,r,mat,f,roof):
    a=unreal.Vector(*a);b=unreal.Vector(*b);v=b-a
    return primitive('SupportRod',tuple((getattr(a,k)+getattr(b,k))/2 for k in 'xyz'),(2*r,2*r,v.length()),cylinder,mat,f,roof,unreal.MathLibrary.make_rot_from_z(v))
def anchor(x,y,low,f,roof):
    top,parent=ceiling(x,y,low+1)
    rod((x,y,low),(x,y,top+.4),.42,silver,f,roof)
    plate=primitive('CeilingAnchor',(x,y,top-.15),(8,8,1.1),cube,zinc,f,roof)
    pc,pe=plate.get_actor_bounds(False)
    gap=top-(pc.z+pe.z);assert -.51<=gap<=.1
    mounts.append({'type':'ceiling','surface':parent.get_actor_label(),'point':[x,y,top],'gap_cm':gap})
    return parent
def tray(key,pos,length=400,yaw=90,f=0,roof=False,width=1):
    a=spawn(key,pos,yaw,scale=(length/200,width,1),f=f,roof=roof)
    if not a:return
    spawn('CableBundle_2m',pos,yaw,scale=(length/200,width,1),f=f,roof=roof,detail=True)
    angle=math.radians(yaw);tx,ty=math.cos(angle),math.sin(angle);nx,ny=-ty,tx
    for distance in [length*.25,length*.75]:
        x,y=pos[0]+distance*tx,pos[1]+distance*ty
        primitive('TrayCrossbar',(x,y,pos[2]-2),(4,52*width,4),cube,zinc,f,roof,unreal.Rotator(yaw=yaw))
        for side in [-1,1]:anchor(x+nx*side*24*width,y+ny*side*24*width,pos[2],f,roof)
def light(pos,f=0,roof=False):
    x,y,z=pos;a=spawn('LEDLinear_1200',pos,f=f,roof=roof)
    if not a:return
    for xx in [x+6,x+114]:anchor(xx,y,z+4.5,f,roof)
    # Power feed from the fixture driver up to the mounting surface.
    top,_=ceiling(x+60,y,z+6);rod((x+60,y,z+5.2),(x+60,y,top),.22,cablemat,f,roof)
def pipe(pos,length=400,yaw=0,f=0,roof=False):
    a=spawn('FirePipeStraight_2m',pos,yaw,scale=(length/200,1,1),f=f,roof=roof)
    if not a:return
    angle=math.radians(yaw)
    for distance in [length*.25,length*.75]:
        x=pos[0]+math.cos(angle)*distance;y=pos[1]+math.sin(angle)*distance
        # A U cradle preserves pipe clearance; only the suspension rod changes length.
        nx,ny=-math.sin(angle),math.cos(angle)
        primitive('PipeCradle',(x,y,pos[2]-4.7),(3,11,1),cube,zinc,f,roof,unreal.Rotator(yaw=yaw))
        primitive('PipeCradleTop',(x,y,pos[2]+6),(3,11,1),cube,zinc,f,roof,unreal.Rotator(yaw=yaw))
        for side in [-1,1]:
            xx,yy=x+nx*side*4.9,y+ny*side*4.9
            rod((xx,yy,pos[2]-4.7),(xx,yy,pos[2]+6),.4,silver,f,roof)
        anchor(x,y,pos[2]+6.5,f,roof)
    for distance in [length*.35,length*.85]:
        x=pos[0]+math.cos(angle)*distance;y=pos[1]+math.sin(angle)*distance
        # A downcomer joins the pipe center to the sprinkler inlet.
        rod((x,y,pos[2]),(x,y,pos[2]-9),1.25,materials[norm('WS Fire pipe red')],f,roof)
        spawn('SprinklerHead',(x,y,pos[2]-9),f=f,roof=roof,detail=True)
# Closely spaced roof trunks, cross routes and paired sprinkler lanes.
for x in [-1550,-750,0,750,1550,2350]:
    for y in range(-1800,1800,400):tray('TrayStraight_2m',(x,y,1115),roof=True)
for y in [-1050,50,1050]:
    for x in range(-1750,2250,400):tray('TrayStraight_2m',(x,y,1100),yaw=0,roof=True,width=.75)
for y in [-1450,-700,100,900,1600]:
    for x in range(-1800,2600,400):pipe((x,y,1065),roof=True)
for x in [-1750,2500]:
    for y in range(-1650,1350,300):
        a=spawn('DuctStraight_1500',(x,y,1140),yaw=90,scale=(2,1,1),roof=True)
        if a:
            for yy in [y+50,y+250]:
                primitive('DuctSaddle',(x,yy,1126),(43,5,3),cube,zinc,0,True)
                for xx in [x-20,x+20]:anchor(xx,yy,1127,0,True)
    spawn('DuctElbow90',(x,1350,1140),yaw=90,roof=True);spawn('DuctGrille',(x-45,1395,1140),yaw=90,roof=True)
for x in [-1450,-650,350,1250,2200]:
    for y in [-1550,-850,-150,550,1250]:
        top,_=ceiling(x+60,y,1040);light((x,y,top-25),roof=True)
# Deck-by-deck dense services. The actual ceiling at each fixture controls its height.
for f in [1,2]:
    z=f*400-70
    for x in [-1450,-850,850,1450]:
        for y in range(-1650,1150,400):tray('TrayStraight_2m',(x,y,z),f=f,width=.75)
    for x in [-1250,-700,700,1250]:
        for y in [-1450,-850,-250,350,950]:
            top,_=ceiling(x+60,y,f*400-80)
            if top>f*400+5:continue
            light((x,y,top-12),f=f)
    for x in [-1600,-1000,1000,1600]:
        for y in range(-1600,800,400):pipe((x,y,z-5),yaw=90,f=f)
# Wall-mounted equipment. Cabinet mounting feet, not just its center, meet the inner wall plane.
walls={}
for name in ['WH_Wall_E','WH_Wall_W','WH_Wall_N']:
    wall=named[name];c,e=wall.get_actor_bounds(False)
    if name.endswith('_E'):point=(c.x-e.x,0);normal=(-1,0);yaw=-90;direction=(0,1)
    elif name.endswith('_W'):point=(c.x+e.x,0);normal=(1,0);yaw=90;direction=(0,1)
    else:point=(0,c.y-e.y);normal=(0,-1);yaw=0;direction=(1,0)
    walls[name]=(wall,point,normal,yaw,direction)
def wall_mount(key,profile,t,z,f=0):
    wall,p,n,yaw,d=profile;b=meshes[key].get_bounds();back=b.origin.y+b.box_extent.y
    x=p[0]+d[0]*t+n[0]*(back-.2);y=p[1]+d[1]*t+n[1]*(back-.2)
    a=spawn(key,(x,y,z),yaw=yaw,f=f,parent=wall)
    if a:
        ac,ae=a.get_actor_bounds(False);face=(p[0]+d[0]*t,p[1]+d[1]*t)
        gap=ac.x*n[0]+ac.y*n[1]-abs(n[0])*ae.x-abs(n[1])*ae.y-(face[0]*n[0]+face[1]*n[1])
        assert -.5<=gap<=.1,(key,gap)
        mounts.append({'type':'wall','surface':wall.get_actor_label(),'point':[face[0],face[1],z],'gap_cm':gap})
    return a
for name,profile in walls.items():
    wall,p,n,yaw,d=profile
    locations=[-1250,-450,450,1250] if name!='WH_Wall_N' else [-1500,-600,1700,2450]
    for t in locations:
        a=wall_mount('DistributionCabinet',profile,t,145)
        if not a:continue
        loc=a.get_actor_location();angle=math.radians(yaw)
        spawn('DistributionDoor',(loc.x-28*math.cos(angle),loc.y-28*math.sin(angle),145),yaw=yaw,parent=wall)
        wall_mount('JunctionBox',profile,t+50,220)
        for u in [-18,0,18]:
            x=p[0]+d[0]*(t+u)+n[0]*2.0;y=p[1]+d[1]*(t+u)+n[1]*2.0
            spawn('ConduitStraight_1m',(x,y,190),yaw=yaw,pitch=90,scale=(1.48,1,1),detail=True,parent=wall)
            for zz in [210,275,330]:
                primitive('WallConduitClamp',(x,y,zz),(4,4,1.1),cube,zinc,0,False)
                rod((p[0]+d[0]*(t+u),p[1]+d[1]*(t+u),zz),(x,y,zz),.3,silver,0,False)
    # Three wall trunk levels with visible cantilever brackets.
    for f,z in [(0,340),(1,740),(2,1080)]:
        for t in range(-1700,1500,400):
            x=p[0]+d[0]*t+n[0]*20;y=p[1]+d[1]*t+n[1]*20
            route_yaw=90 if d==(0,1) else 0
            spawn('TrayStraight_2m',(x,y,z),yaw=route_yaw,scale=(2,.75,.75),f=f,roof=f==2,parent=wall)
            spawn('CableBundle_2m',(x,y,z),yaw=route_yaw,scale=(2,.75,.75),f=f,roof=f==2,detail=True,parent=wall)
            for distance in [100,300]:
                pt=(p[0]+d[0]*(t+distance)+n[0]*2,p[1]+d[1]*(t+distance)+n[1]*2,z-2.25)
                inward_yaw=math.degrees(math.atan2(n[1],n[0]));a=spawn('WallBracket',pt,yaw=inward_yaw,f=f,roof=f==2,detail=True,parent=wall)
                if a:mounts.append({'type':'wall_bracket','surface':wall.get_actor_label(),'point':[pt[0]-n[0]*2,pt[1]-n[1]*2,z],'gap_cm':0})
    # Vertical trunk riser near the wall end connects all three tray levels.
    t=-1600;x=p[0]+d[0]*t+n[0]*4;y=p[1]+d[1]*t+n[1]*4
    for z in range(340,1040,100):spawn('ConduitStraight_1m',(x,y,z),pitch=90,detail=True,parent=wall)
# Existing light fixtures also receive measured mounts rather than remaining unsupported.
legacy=0
for name,a in named.items():
    if name.startswith(('WH_LightFixture_','WH_SideFixture_')):
        c,e=a.get_actor_bounds(False)
        for side in [-1,1]:
            x=c.x+side*min(e.x*.65,40);y=c.y
            try:anchor(x,y,c.z+e.z-.1,2,True);legacy+=1
            except AssertionError:pass
# Restore rubber guards on ground-rooted interior columns.
for name,a in named.items():
    if 'Column_' not in name:continue
    c,e=a.get_actor_bounds(False)
    if c.z-e.z>10 or e.x>30 or e.y>30 or name.startswith('WH_Column_'):continue
    if c.x<-1650 and -1320<c.y<-300:continue
    spawn('RubberColumnWrap_450',(c.x,c.y,0),scale=((2*e.x+.6)/45,(2*e.y+.6)/45,1))
# Merge only support hardware into floor/roof batches to avoid thousands of tiny actors.
batch_results=[]
for (f,roof),items in supports.items():
    options=unreal.MergeStaticMeshActorsOptions();options.set_editor_property('destroy_source_actors',True);options.set_editor_property('spawn_merged_actor',True);options.set_editor_property('new_actor_label','WS_SupportBatch_'+str(f)+('_Roof' if roof else ''))
    options.set_editor_property('base_package_name',BASE+'/SupportBatches/SM_Support_'+str(f)+('_Roof' if roof else ''))
    settings=options.get_editor_property('mesh_merging_settings');settings.set_editor_property('merge_materials',False);settings.set_editor_property('generate_light_map_uv',False);settings.set_editor_property('merge_physics_data',False);options.set_editor_property('mesh_merging_settings',settings)
    result=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).merge_static_mesh_actors(items,options)
    merged=result[-1] if isinstance(result,tuple) else result
    assert isinstance(merged,unreal.StaticMeshActor),(type(result),result)
    tag(merged,f,roof,True);batch_results.append({'floor':f,'roof':roof,'merged_support_parts':len(items)})
after={a.get_actor_label():a for a in actors.get_all_level_actors()}
for label,transform in before.items():assert unreal.MathLibrary.nearly_equal_transform_transform(after[label].get_actor_transform(),transform,.001,.001,.001),label
all_services=[a for n,a in after.items() if n.startswith('WS_')]
assert all(abs(m['gap_cm'])<=.5 for m in mounts)
assert level.save_current_level();assets.save_directory(BASE,False,True)
report={'date':'2026-10-14','placed_actors':len(all_services),'layout_revision':'dense_surface_mounted','counts':counts,'mount_checks':len(mounts),'max_mount_gap_cm':max(abs(m['gap_cm']) for m in mounts),'support_batches':batch_results,'legacy_light_mounts':legacy,'shaft_exclusions':excluded,'original_actor_transforms_preserved':True,'map_saved':True,'decorative_collision':'NoCollision','runtime_and_performance_tested':False}
(OUT/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');(OUT/'Mounts.json').write_text(json.dumps(mounts,indent=2),encoding='utf-8')
print('DENSE_SERVICES_SAVED',json.dumps(report));unreal.SystemLibrary.quit_editor()
