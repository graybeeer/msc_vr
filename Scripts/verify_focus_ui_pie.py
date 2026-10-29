"""Default automatic warehouse UI, real saved-map PIE and native Slate callbacks.

This does NOT inject OS mouse/keyboard input or prove forklift transport. It
captures actual game Slate at requested 1600x900 and 1280x720 (embedded PIE may
retain another size). Review the PNGs for layout/clipping; dimensions alone are
not a visual pass. No maps or user operations preferences are saved.
-FocusUIStrictResolution requires exact viewport sizes. -KeepTestEditorOpen
keeps the editor open after cleanup. -WarehousePlayTest is required to isolate
campaign progress from the user's real save. Run with rendering, not NullRHI.
"""
import hashlib
import json
from pathlib import Path
import struct
import time
import traceback

import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/FocusUI'
OUT.mkdir(parents=True, exist_ok=True)
MENU = '/Game/Warehouse/Play/Maps/MainMenu'
WAREHOUSE = '/Game/FirstPerson/Lvl_FirstPerson'
CAMPAIGN = '/Game/Warehouse/Play/Maps/Stage01'
RESOLUTIONS = ((1600, 900), (1280, 720))
COMMAND = unreal.SystemLibrary.get_command_line().lower()
KEEP = '-keeptesteditoropen' in COMMAND
STRICT = '-focusuistrictresolution' in COMMAND
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
play_settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
original_window = (play_settings.get_editor_property('NewWindowWidth'), play_settings.get_editor_property('NewWindowHeight'))
original_legacy = unreal.SystemLibrary.get_console_variable_int_value('Warehouse.Computer.LegacyUI')
started = phase_at = time.monotonic()
phase = 'start'
handle = None
game = pc = pawn = computer = director = stage = None
original_manual = None
original_files = original_preferences = None
resolution_index = 0
stage_budget = stage_goal = stage_supply = stage_shipping_count = 0
stage_started = 0
extra_vehicles = []
checks, screens = [], []
report = dict(passed=False, checks=checks, screenshots=screens, osInputTested=False,
              nativeTransportTested=False, visualReviewRequired=True,
              scope='Saved main-menu/warehouse/Stage01 PIE; native Slate callbacks, mode routing, editing rules and finite campaign start')


def maps():
    paths = []
    for name in (MENU, WAREHOUSE, CAMPAIGN):
        relative = name.removeprefix('/Game/')
        paths.append(ROOT / 'Content' / (relative + '.umap'))
        for folder in ('__ExternalActors__', '__ExternalObjects__'):
            paths.extend((ROOT / 'Content' / folder / relative).rglob('*'))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


def preferences():
    paths = [ROOT / 'Config/DefaultGame.ini', ROOT / 'Saved/Config/WindowsEditor/Game.ini',
             ROOT / 'Saved/Config/Windows/Game.ini', ROOT / 'Saved/SaveGames/WarehousePlayProgress_v1.sav']
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in paths}


def check(message):
    checks.append(message)
    print('FOCUS_UI_CHECK_PASSED', message)


def enter(value):
    global phase, phase_at
    phase, phase_at = value, time.monotonic()


def console(value):
    unreal.SystemLibrary.execute_console_command(game or editor.get_editor_world(), value, pc)


def ui(command='snapshot', focus=True, required=True):
    folder = OUT if focus else ROOT / 'Saved/Testing/ComputerDashboard'
    state_file = folder / 'State.json'
    state_file.unlink(missing_ok=True)
    console(('Warehouse.Focus.Test ' if focus else 'Warehouse.Computer.Test ') + command)
    if not required:
        assert not state_file.exists(), 'Closed screen still accepted UI callbacks'
        return
    assert state_file.is_file(), ('Expected Slate screen did not answer', command, focus)
    state = json.loads(state_file.read_text(encoding='utf-8-sig'))
    report.setdefault('uiStates', []).append(dict(focus=focus, command=command, state=state))
    return state


def capture(name):
    unreal.load_module('AutomationUtils')
    capture_name = name + '_' + str(time.time_ns())
    incoming = ROOT / 'Saved/Automation/Incoming/FocusUI' / capture_name
    options = unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features', False)
    options.set_editor_property('disable_tonemapping', False)
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(capture_name, map_name_override='FocusUI')
    assert not list(incoming.rglob('*.png')), 'Image appeared before explicit UI capture'
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game, unreal.LatentActionInfo(), capture_name, options)
    images = list(incoming.rglob('*.png'))
    assert len(images) == 1, ('No unique actual Slate screenshot', name)
    data = images[0].read_bytes()
    assert len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n' and data[-8:-4] == b'IEND', 'Incomplete UI PNG'
    width, height = struct.unpack('>II', data[16:24])
    destination = OUT / (name + '.png')
    destination.write_bytes(data)
    expected = RESOLUTIONS[resolution_index]
    screens.append(dict(path=str(destination), width=width, height=height, expected=list(expected),
                        actualSlate=True, visualReviewRequired=True))
    assert width >= 960 and height >= 540, ('Viewport too small for review', width, height)
    if STRICT:
        assert (width, height) == expected, ('Use floating PIE to meet requested size', (width, height), expected)


def complete(ok, detail):
    global handle
    play_settings.set_editor_property('NewWindowWidth', original_window[0])
    play_settings.set_editor_property('NewWindowHeight', original_window[1])
    report.update(passed=ok, message=detail, wall_seconds=round(time.monotonic()-started, 2),
                  savedMapUnchanged=maps() == original_files,
                  userPreferencesUnchanged=preferences() == original_preferences)
    report['passed'] = bool(ok and report['savedMapUnchanged'] and report['userPreferencesUnchanged'])
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('FOCUS_UI_PIE_' + ('PASSED' if report['passed'] else 'FAILED'), detail)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if not KEEP:
        unreal.SystemLibrary.quit_editor()


def finish(ok, detail):
    report.update(candidate_passed=ok, message=detail)
    end_session('end')


def end_session(next_phase):
    if pc and unreal.SystemLibrary.is_valid(pc):
        pc.close_warehouse_computer()
        pc.set_layout_editing(False)
    if director and unreal.SystemLibrary.is_valid(director) and original_manual is not None:
        director.set_manual_operations(original_manual, False)
    console('Warehouse.Computer.LegacyUI %d' % original_legacy)
    level.editor_request_end_play()
    enter(next_phase)


def _tick(dt):
    global game, pc, pawn, computer, director, stage, original_files, original_preferences
    global original_manual, resolution_index
    global stage_budget, stage_goal, stage_supply, stage_shipping_count, stage_started, extra_vehicles
    assert time.monotonic()-started < 600, ('Focus UI test timed out', phase)
    age = time.monotonic()-phase_at
    if phase == 'start':
        original_files, original_preferences = maps(), preferences()
        assert '-warehouseplaytest' in COMMAND, 'Use -WarehousePlayTest to protect real campaign progress'
        assert len(original_files) >= 3, 'Saved menu/warehouse/campaign maps missing'
        assert not editor.get_game_world(), 'Start this test with PIE stopped'
        assert level.load_level(MENU)
        play_settings.set_editor_property('NewWindowWidth', RESOLUTIONS[0][0])
        play_settings.set_editor_property('NewWindowHeight', RESOLUTIONS[0][1])
        level.editor_request_begin_play()
        enter('menu')
        return
    if phase in ('menu_end', 'warehouse_end', 'end'):
        if editor.get_game_world() or age < 1:
            return
        game = pc = pawn = computer = director = stage = None
        original_manual = None
        if phase == 'end':
            complete(report['candidate_passed'], report['message'])
        elif phase == 'menu_end':
            assert level.load_level(WAREHOUSE)
            level.editor_request_begin_play()
            enter('warehouse')
        else:
            assert level.load_level(CAMPAIGN)
            level.editor_request_begin_play()
            enter('campaign')
        return
    game = editor.get_game_world()
    if not game:
        return
    if phase == 'menu' and age > 1:
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        assert pc and pc.is_main_menu()
        assert not unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseFocusDirector), 'Warehouse automation leaked into main menu'
        assert not pc.open_forklift_commands(), 'Menu allowed warehouse command screen'
        check('Main-menu game world retains menu scope and has no warehouse operations director')
        level.editor_request_end_play()
        enter('menu_end')
    elif phase == 'warehouse':
        if unreal.GameplayStatics.get_time_seconds(game) < 4:
            return
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        pawn = pc.get_controlled_pawn()
        directors = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseFocusDirector)
        computers = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseComputer)
        assert len(directors) == 1 and len(computers) == 1 and pawn
        director, computer = directors[0], computers[0]
        original_manual = director.is_manual_operations()
        report['initialSavedManualPreference'] = original_manual
        director.set_manual_operations(False, False)
        assert not director.is_manual_operations()
        assert not pc.is_warehouse_computer_open(), 'Automatic popup blocked initial player controls'
        assert not pc.is_main_menu()
        computer.refresh_state()
        fleet = list(computer.get_editor_property('fleet'))
        assert fleet and not any(v.get_editor_property('bPowered') for v in fleet), 'Vehicles started before user command'
        assert pc.open_forklift_commands(), 'Remote command entry failed'
        assert pc.is_warehouse_computer_open() and pc.is_move_input_ignored() and pc.is_look_input_ignored()
        state = ui()
        assert state['focus_screen'] and not state['manual_operations'] and state['fleet'] == len(fleet)
        assert state['inbound_selected'], 'The default tab must explain receiving transport'
        assert not unreal.GameplayStatics.is_game_paused(game), 'Command screen paused whole simulation'
        console('DisableAllScreenMessages')
        console('r.SetRes 1600x900w')
        check('Existing warehouse automatically owns one director; player starts unobstructed and forklift commands open remotely')
        enter('focus_high')
    elif phase == 'focus_high' and age > 1:
        capture('1600x900_DefaultFocus')
        state = ui('tab outbound')
        assert not state['inbound_selected'] and state['outbound_visible'] and state['pallets'] > 0
        enter('outbound_capture')
    elif phase == 'outbound_capture' and age > .7:
        capture('1600x900_OutboundTab')
        assert ui('tab inbound')['inbound_selected']
        # These extra actors exist only to verify the actual fleet viewport.
        # Disable their ticks/contacts; this is a UI fixture, not a vehicle test.
        ui('spawn_test_fleet 4')
        computer.refresh_state()
        assert ui()['fleet'] >= 6
        ui('fleet_end')
        enter('fleet_capture')
    elif phase == 'fleet_capture' and age > .7:
        state = ui()
        assert state['fleet'] >= 6 and state['fleet_scroll_offset'] > 0, state
        capture('1600x900_SixVehiclesScrolled')
        ui('vehicle_stop_work 0')
        ui('stop_work')
        assert not any(v.get_editor_property('bPowered') for v in computer.get_editor_property('fleet'))
        ui('clear_test_fleet')
        extra_vehicles = []
        computer.refresh_state()
        ui('fleet_top')
        check('Inbound/outbound tabs filter stock and six vehicle rows scroll in a bounded panel; explicit work-stop callbacks remain distinct from pause')
        enter('focus_controls')
    elif phase == 'focus_controls' and age > .7:
        ui('start')
        assert any(v.get_editor_property('bPowered') for v in computer.get_editor_property('fleet')), 'Shared start callback did not power a vehicle'
        ui('stop')
        assert not any(v.get_editor_property('bPowered') for v in computer.get_editor_property('fleet')), 'Shared stop callback did not pause vehicles'
        check('Actual focus start/stop callback path changes vehicle operating state')
        pc.close_warehouse_computer()
        console('Warehouse.Computer.LegacyUI 1')
        # Exercise the actual nearby E handler, whose entry has a separate legacy selector.
        pawn.set_actor_location(computer.get_actor_location()-computer.get_actor_forward_vector()*210+unreal.Vector(0, 0, 96.5), False, True)
        camera = pawn.get_component_by_class(unreal.CameraComponent)
        target = computer.get_actor_transform().transform_location(unreal.Vector(8.7, 0, 111))
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), target))
        enter('legacy_aim')
    elif phase == 'legacy_aim' and age > 1:
        assert pawn.get_interaction_target() == computer, ('Computer not targeted for native E', pawn.get_interaction_target())
        pawn.toggle_carry()
        assert pc.is_warehouse_computer_open()
        assert ui()['focus_screen'], 'Legacy CVar exposed office UI in automatic mode'
        enter('legacy_capture')
    elif phase == 'legacy_capture' and age > .7:
        capture('1600x900_FocusDespiteLegacyOption')
        check('Native E handler and LegacyUI=1 still route automatic mode to forklift-only screen')
        pc.close_warehouse_computer()
        console('Warehouse.Computer.LegacyUI 0')
        director.set_manual_operations(True, False)
        assert director.is_manual_operations() and pc.open_forklift_commands()
        assert ui(focus=False)['page'] == 0
        assert ui('tab 5', focus=False)['page'] == 5, 'Manual mode did not restore warehouse operations tab'
        enter('manual_capture')
    elif phase == 'manual_capture' and age > .7:
        ui(required=False)
        capture('1600x900_ManualWarehouseOperations')
        pc.close_warehouse_computer()
        director.set_manual_operations(False, False)
        assert pc.open_forklift_commands() and ui()['focus_screen']
        check('Session-only manual option restores existing office tabs; switching back removes them without changing saved preferences')
        resolution_index = 1
        console('r.SetRes 1280x720w')
        enter('focus_low')
    elif phase == 'focus_low' and age > 1:
        ui(focus=False, required=False)
        capture('1280x720_DefaultFocus')
        ui('layout')
        assert not pc.is_warehouse_computer_open() and pc.is_observer_view() and pc.is_layout_editing()
        assert unreal.GameplayStatics.is_game_paused(game)
        pc.set_construction_editing(True)
        construction = pc.get_construction_editor()
        assert construction and construction.get_editor_property('bActive'), 'Building terrain editor was disabled in default mode'
        enter('building_capture')
    elif phase == 'building_capture' and age > .7:
        capture('1280x720_BuildingEditing')
        pc.set_construction_editing(False)
        assert pc.is_layout_editing()
        enter('layout_capture')
    elif phase == 'layout_capture' and age > .7:
        capture('1280x720_ObjectRearrangement')
        assert pc.open_forklift_commands(), 'Observer command shortcut failed'
        assert pc.is_observer_view() and not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
        ui('player')
        assert not pc.is_warehouse_computer_open() and not pc.is_observer_view()
        assert pc.get_controlled_pawn() == pawn and pc.get_view_target() == pawn
        assert not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
        check('Actual focus navigation callbacks preserve player possession, editing pause/resume and unrestricted building/object editors')
        end_session('warehouse_end')
    elif phase == 'campaign' and age > 1:
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        stages = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePlayStage)
        directors = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseFocusDirector)
        computers = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseComputer)
        assert pc and len(stages) == len(directors) == len(computers) == 1
        stage, director, computer = stages[0], directors[0], computers[0]
        pawn = pc.get_controlled_pawn()
        original_manual = director.is_manual_operations()
        director.set_manual_operations(False, False)
        assert stage.get_editor_property('stage_index') == 1 and not pc.is_main_menu()
        stage_budget = stage.get_editor_property('budget_won')
        stage_goal = stage.get_editor_property('delivery_goal')
        assert stage_budget > 0 and stage_goal > 0 and not stage.get_editor_property('operating')
        shipping = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseShippingSystem)
        stage_shipping_count = len(shipping)
        stage_supply = sum(s.get_editor_property('generated_units') for s in shipping)
        assert pc.open_forklift_commands()
        state = ui()
        assert state['focus_screen'] and state['stage'] == 1 and not state['outbound_visible']
        assert not state['manual_operations']
        console('r.SetRes 1280x720w')
        check('Stage01 uses the same default simplified screen with campaign goal/budget and no unsupported outbound commands')
        enter('campaign_capture')
    elif phase == 'campaign_capture' and age > .7:
        capture('1280x720_Stage01Focus')
        ui('layout')
        assert pc.is_observer_view() and pc.is_layout_editing()
        pc.set_construction_editing(True)
        construction = pc.get_construction_editor()
        assert not construction or not construction.get_editor_property('bActive'), 'Campaign UI unlocked fixed building editing'
        assert stage.get_editor_property('budget_won') == stage_budget, 'Viewing equipment editor charged budget'
        assert pc.open_forklift_commands()
        ui('start')
        assert stage.get_editor_property('operating'), 'Focus start did not invoke finite Stage.StartOperation'
        assert stage.get_editor_property('budget_won') == stage_budget
        stage_started = unreal.GameplayStatics.get_time_seconds(game)
        enter('campaign_operating')
    elif phase == 'campaign_operating':
        if unreal.GameplayStatics.get_time_seconds(game)-stage_started < 3:
            assert age < 90, 'Campaign simulation did not advance after native start'
            return
        arrived = stage.get_editor_property('arrived')
        assert 0 <= arrived <= stage_goal and stage.get_editor_property('delivery_goal') == stage_goal
        assert stage.get_editor_property('budget_won') == stage_budget
        shipping = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseShippingSystem)
        assert len(shipping) == stage_shipping_count and sum(s.get_editor_property('generated_units') for s in shipping) == stage_supply, 'Director added continuous warehouse supply to campaign'
        report['campaign'] = dict(stage=1, budgetBefore=stage_budget, budgetAfter=stage.get_editor_property('budget_won'),
                                  arrived=arrived, goal=stage_goal, shippingSystems=stage_shipping_count,
                                  continuousSupply=stage_supply, completedGoalTested=False)
        check('Campaign start retains finite arrival goal and unchanged budget; office director does not create extra supply')
        pc.close_warehouse_computer()
        director.set_manual_operations(True, False)
        assert pc.open_forklift_commands() and ui(focus=False)['page'] == 0
        pc.close_warehouse_computer()
        director.set_manual_operations(False, False)
        assert pc.open_forklift_commands() and ui()['stage'] == 1
        enter('campaign_running_capture')
    elif phase == 'campaign_running_capture' and age > .7:
        capture('1280x720_Stage01Operating')
        ui('stop')
        ui('player')
        assert not pc.is_warehouse_computer_open() and not pc.is_observer_view()
        assert pc.get_controlled_pawn() == pawn and not pc.is_move_input_ignored()
        enter('campaign_hud_capture')
    elif phase == 'campaign_hud_capture' and age > .7:
        capture('1280x720_Stage01OriginalHUD')
        check('Campaign manual/default screen switch retains player controls and original stage HUD')
        finish(True, 'Warehouse and campaign simplified/manual routing, Slate callbacks, editing scope, finite campaign start and saved-file preservation verified; PNG visual review still required')


_busy = False


def tick(dt):
    global _busy
    if _busy:
        return
    _busy = True
    try:
        _tick(dt)
    except Exception:
        detail = traceback.format_exc()
        if phase == 'end':
            complete(False, detail)
        else:
            finish(False, detail)
    finally:
        _busy = False


handle = unreal.register_slate_post_tick_callback(tick)
