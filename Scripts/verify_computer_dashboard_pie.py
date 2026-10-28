"""Saved-map computer dashboard: native interaction, real Slate renders, no map save.

Runs through the same dashboard callbacks as its visible controls. Native calls
are not OS mouse/keyboard input, and this test does not claim AGV transport.
Requests 1600x900 and 1280x720; reports the actual Slate dimensions if an embedded
viewport cannot resize. -DashboardStrictResolution requires an exact match.
-DashboardExternalPIE waits for an externally started floating PIE session.
-KeepTestEditorOpen leaves the editor available after cleanup.
"""
import hashlib
import json
from pathlib import Path
import struct
import time
import traceback

import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/ComputerDashboard'
OUT.mkdir(parents=True, exist_ok=True)
COMMAND = unreal.SystemLibrary.get_command_line().lower()
KEEP = '-keeptesteditoropen' in COMMAND
EXTERNAL = '-dashboardexternalpie' in COMMAND
STRICT_RESOLUTION = '-dashboardstrictresolution' in COMMAND
RESOLUTIONS = ((1600, 900), (1280, 720))
PAGE_NAMES = ('Dispatch', 'Shipping', 'TasksAndFleet', 'Held', 'Experiments', 'WarehouseProcesses')
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
play_settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
original_window_size = (play_settings.get_editor_property('NewWindowWidth'), play_settings.get_editor_property('NewWindowHeight'))
started = time.monotonic()
phase = 'start'
phase_at = started
handle = None
game = pc = pawn = computer = None
original_files = {}
entry_pose = None
refresh_before = 0
refresh_simulation_seconds = 0
opened_simulation_seconds = 0
selected_cargo_id = ''
selected_ids = []
held_before = 0
resolution_index = page_index = 0
checks = []
screens = []
report = dict(passed=False, checks=checks, screenshots=screens, osInputTested=False,
              nativeTransportTested=False, savedMapUnchanged=False,
              exactResolutionsRequired=STRICT_RESOLUTION,
              scope='Real saved-map PIE, E interaction handler, actual dashboard callbacks and rendered game Slate viewport')


def map_files():
    paths = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
    for folder in ('__ExternalActors__', '__ExternalObjects__'):
        paths.extend((ROOT / 'Content' / folder / 'FirstPerson/Lvl_FirstPerson').rglob('*'))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


def check(message):
    checks.append(message)
    print('COMPUTER_DASHBOARD_CHECK_PASSED', message)


def enter(value):
    global phase, phase_at
    phase, phase_at = value, time.monotonic()


def console(value):
    unreal.SystemLibrary.execute_console_command(game or editor.get_editor_world(), value, pc)


def ui(command='snapshot'):
    """The editor-only command invokes actual Slate buttons/list controls."""
    state_file = OUT / 'State.json'
    state_file.unlink(missing_ok=True)
    console('Warehouse.Computer.Test ' + command)
    assert state_file.is_file(), ('Actual dashboard control did not return a snapshot', command)
    state = json.loads(state_file.read_text(encoding='utf-8-sig'))
    report.setdefault('uiStates', []).append(dict(command=command, state=state))
    return state


def complete(ok, detail):
    global handle
    report.update(passed=ok, message=detail, wall_seconds=round(time.monotonic() - started, 2))
    report['requestedResolutionsObserved'] = bool(screens) and all((s['width'], s['height']) == tuple(s['expected']) for s in screens)
    report['actualResolutions'] = [list(size) for size in sorted({(s['width'], s['height']) for s in screens})]
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('COMPUTER_DASHBOARD_PIE_' + ('PASSED' if ok else 'FAILED'), detail)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if not KEEP:
        unreal.SystemLibrary.quit_editor()


def request_finish(ok, detail):
    report.update(candidate_passed=ok, message=detail)
    if pc and pc.is_warehouse_computer_open():
        pc.close_warehouse_computer()
    console('Warehouse.Computer.LegacyUI 0')
    level.editor_request_end_play()
    enter('end_play')


def verify_inventory():
    computer.refresh_state()
    inventory = list(computer.get_editor_property('inventory'))
    eligible = {a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo)
                if not a.actor_has_tag('LogisticsInTransit')}
    assert eligible and {row.cargo for row in inventory} == eligible, 'Inventory differs from eligible physical cargo'
    assert len(inventory) == len(eligible) == len({row.cargo_id for row in inventory}), 'Duplicate inventory actor or identity'
    report['eligible_cargo'] = len(eligible)


def inventory_interval_elapsed(now, wall_age):
    # Actor tick intervals use simulation time. Rendering can take several
    # wall-clock seconds to advance one second of the fixed-step world.
    elapsed = now - refresh_simulation_seconds
    observation = dict(phase=phase, wall_seconds=round(wall_age, 3),
                       simulation_seconds=round(elapsed, 3),
                       refresh_before=refresh_before,
                       refresh_after=computer.get_editor_property('refresh_count'))
    if elapsed < 1.25:
        if wall_age > 60:
            report.setdefault('refreshObservations', []).append(observation)
            raise AssertionError(('Simulation did not advance through the inventory tick interval', observation))
        return False
    report.setdefault('refreshObservations', []).append(observation)
    return True


def capture(name):
    # Engine UI capture reads the actual world's SViewport. A later queued
    # scene-only screenshot must never be accepted as dashboard evidence.
    unreal.load_module('AutomationUtils')
    capture_name = name + '_' + str(time.time_ns())
    incoming = ROOT / 'Saved/Automation/Incoming/ComputerDashboard' / capture_name
    options = unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features', False)
    options.set_editor_property('disable_tonemapping', False)
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(
        capture_name, map_name_override='ComputerDashboard')
    assert not list(incoming.rglob('*.png')), 'Screenshot writer ran before explicit Slate capture'
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game, unreal.LatentActionInfo(), capture_name, options)
    images = list(incoming.rglob('*.png'))
    assert len(images) == 1, ('No unique synchronous UI image', name)
    data = images[0].read_bytes()
    assert len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n' and data[-8:-4] == b'IEND', 'Incomplete PNG'
    width, height = struct.unpack('>II', data[16:24])
    expected = RESOLUTIONS[resolution_index]
    destination = OUT / (name + '.png')
    destination.write_bytes(data)
    screens.append(dict(path=str(destination), width=width, height=height, expected=list(expected),
                        actualSlate=True, visualReviewRequired=True))
    assert width >= 960 and height >= 540, ('Actual game viewport is too small for meaningful screen review', (width, height))
    if STRICT_RESOLUTION:
        assert (width, height) == expected, ('Actual game viewport has wrong size; use floating PIE', (width, height), expected)


def _tick(dt):
    global game, pc, pawn, computer, original_files, entry_pose, refresh_before
    global opened_simulation_seconds, refresh_simulation_seconds, resolution_index, page_index, selected_cargo_id, selected_ids, held_before
    assert time.monotonic() - started < 600, ('Dashboard test timed out', phase)
    age = time.monotonic() - phase_at
    if phase == 'start':
        original_files = map_files()
        assert original_files, 'No source map packages found'
        assert level.load_level(LEVEL)
        unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
        console('Warehouse.Computer.LegacyUI 0')
        if EXTERNAL:
            play_settings.set_editor_property('NewWindowWidth', RESOLUTIONS[0][0])
            play_settings.set_editor_property('NewWindowHeight', RESOLUTIONS[0][1])
            print('COMPUTER_DASHBOARD_EXTERNAL_PIE_READY: start an InEditorFloating PIE session')
        else:
            level.editor_request_begin_play()
        enter('settle')
        return
    if phase == 'end_play':
        if editor.get_game_world() or age < 1:
            return
        play_settings.set_editor_property('NewWindowWidth', original_window_size[0])
        play_settings.set_editor_property('NewWindowHeight', original_window_size[1])
        current = map_files()
        report['savedMapUnchanged'] = current == original_files
        report['savedFilesChecked'] = len(current)
        if current != original_files:
            complete(False, report['message'] + '\nSaved source map packages changed during UI-only test')
        else:
            complete(report['candidate_passed'], report['message'])
        return
    game = editor.get_game_world()
    if not game:
        return
    now = unreal.GameplayStatics.get_time_seconds(game)
    if phase == 'settle':
        if now < 4:
            return
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        pawn = pc.get_controlled_pawn()
        computers = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseComputer)
        assert len(computers) == 1 and pawn
        computer = computers[0]
        assert pc.set_simulation_speed(1)
        assert not pc.open_warehouse_computer(computer), 'Computer opened before nearby screen interaction'
        # Only the operator is positioned. Stock and equipment are untouched.
        pawn.set_actor_location(computer.get_actor_location() - computer.get_actor_forward_vector()*210 + unreal.Vector(0, 0, 96.5), False, True)
        camera = pawn.get_component_by_class(unreal.CameraComponent)
        target = computer.get_actor_transform().transform_location(unreal.Vector(8.7, 0, 111))
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), target))
        console('DisableAllScreenMessages')
        console('r.SetRes 1600x900w')
        enter('aim')
    elif phase == 'aim' and age > 1:
        assert pawn.get_interaction_target() == computer, ('Screen interaction target mismatch', pawn.get_interaction_target())
        pawn.toggle_carry()
        assert pc.is_warehouse_computer_open(), 'Real E interaction failed to open dashboard'
        assert pc.is_move_input_ignored() and pc.is_look_input_ignored()
        assert not unreal.GameplayStatics.is_game_paused(game)
        assert pc.get_view_target() == pawn and pc.get_controlled_pawn() == pawn
        verify_inventory()
        entry_pose = pawn.get_actor_location()
        refresh_before = computer.get_editor_property('refresh_count')
        refresh_simulation_seconds = now
        opened_simulation_seconds = now
        enter('live')
    elif phase == 'live' and age > 2.5:
        if not inventory_interval_elapsed(now, age):
            return
        assert computer.get_editor_property('refresh_count') > refresh_before, 'Console inventory stopped refreshing'
        assert now > opened_simulation_seconds + 1, 'Console stopped simulation time'
        assert (pawn.get_actor_location() - entry_pose).length() < 10, 'Operator moved while using console'
        check('Saved-map real E opens screen, locks operator only, retains possession and live inventory/time')
        state = ui()
        assert state['page'] == 0, 'Default page must be dispatch'
        assert not state['selectedCargoIds'] and state['selectedPalletCount'] == 0
        assert not state['actionsEnabled']['dispatch'] and not state['actionsEnabled']['outbound'], 'Unselected stock has an enabled send command'
        before = [(t.job_id, t.cancelled, t.completed) for t in computer.get_editor_property('tasks')]
        ui('click dispatch')
        assert before == [(t.job_id, t.cancelled, t.completed) for t in computer.get_editor_property('tasks')], 'Disabled dispatch button created a task'
        no_match = ui('search DASHBOARD-NO-SUCH-STOCK-428137')
        assert no_match['inventoryShown'] == 0, 'Search did not filter visible inventory'
        state = ui('search')
        assert state['inventoryShown'] > 0, 'Clearing search did not restore inventory'
        assert state['vehicleCount'] == len(computer.get_editor_property('fleet'))
        assert state['destinationCount'] == len(computer.get_editor_property('outbound_destinations'))
        check('Actual Slate empty-selection buttons reject dispatch; search and live fleet/destination counts reflect model')
        inventory = list(computer.get_editor_property('inventory'))
        candidate = next(row for row in inventory if row.pallet and not row.reserved)
        selected_cargo_id = candidate.cargo_id
        state = ui('select ' + selected_cargo_id)
        loose = next((row for row in inventory if not row.pallet), None)
        if loose:
            for first, second in ((selected_cargo_id, loose.cargo_id), (loose.cargo_id, selected_cargo_id)):
                assert ui('select ' + first)['accepted']
                state = ui('addSelection ' + second)
                assert state['accepted'] and len(state['selectedCargoIds']) == 2
                assert not state['actionsEnabled']['dispatch'] and '개별 상자' in state['transportBlockReason'], 'Mixed loose/pallet selection silently dispatches partial stock'
            check('Actual mixed loose-carton/pallet selection disables partial dispatch in either selection order')
            state = ui('select ' + selected_cargo_id)
        siblings = [row for row in inventory if row.pallet == candidate.pallet and row.cargo != candidate.cargo]
        if siblings:
            state = ui('addSelection ' + siblings[0].cargo_id)
            assert state['selectedPalletCount'] == 1 and len(state['selectedCargoIds']) == 2, 'Two selected cartons duplicated their common pallet'
            check('Two actual selected inventory rows on one pallet produce a single pallet manifest selection')
            state = ui('select ' + selected_cargo_id)
        selected_ids = list(state['selectedCargoIds'])
        assert selected_cargo_id in selected_ids and state['selectedPalletCount'] == 1, 'Actual row selection failed'
        refresh_before = computer.get_editor_property('refresh_count')
        refresh_simulation_seconds = now
        enter('selection_refresh')
    elif phase == 'selection_refresh' and age > 2.5:
        if not inventory_interval_elapsed(now, age):
            return
        state = ui()
        assert computer.get_editor_property('refresh_count') > refresh_before
        assert sorted(state['selectedCargoIds']) == sorted(selected_ids), 'Periodic refresh lost or changed selected stock'
        assert state['selectedPalletCount'] == 1, 'Single selected pallet duplicated during refresh'
        check('Actual inventory-list selection remains stable through live refresh and counts its pallet once')
        state = ui('tab 3')
        held_before = state['heldCount']
        state = ui('form DASHBOARD-UI-PROBE UI검증상자 12 40 30 25')
        assert state['actionsEnabled']['heldCreate'], 'Valid actual held-cargo form remains disabled'
        state = ui('click heldCreate')
        assert state['accepted'], 'Actual create button rejected valid form'
        enter('held_created')
    elif phase == 'held_created' and age > 1.3:
        state = ui()
        assert state['heldCount'] == held_before + 1, 'Actual form did not add held cargo data'
        state = ui('selectHeld DASHBOARD-UI-PROBE')
        assert state['actionsEnabled']['heldDelete'] and not state['actionsEnabled']['heldConfirm']
        state = ui('click heldDelete')
        assert state['heldCount'] == held_before + 1 and state['actionsEnabled']['heldConfirm'], 'Delete skipped confirmation or removed data early'
        assert ui('click heldConfirm')['accepted'], 'Actual confirmation button rejected selected probe'
        enter('held_deleted')
    elif phase == 'held_deleted' and age > 1.3:
        state = ui()
        assert state['heldCount'] == held_before, 'Confirmed held-cargo deletion did not restore original count'
        assert not state['actionsEnabled']['heldCreate'] and not state['actionsEnabled']['heldDelete']
        check('Actual held-cargo form and create/select/delete/confirm controls update real session data and retain confirmation guard')
        enter('page')
    elif phase == 'page':
        state = ui('tab ' + str(page_index))
        assert state['page'] == page_index, ('Actual tab button did not switch page', page_index, state)
        if page_index == 3:
            assert not state['actionsEnabled']['heldCreate'], 'Blank held-cargo form enables creation'
            assert not state['actionsEnabled']['heldDelete'], 'Unselected held cargo enables deletion'
        enter('capture')
    elif phase == 'capture' and age > .7:
        name = 'Requested%dx%d_%s' % (*RESOLUTIONS[resolution_index], PAGE_NAMES[page_index])
        capture(name)
        page_index += 1
        if page_index < len(PAGE_NAMES):
            enter('page')
        else:
            pc.close_warehouse_computer()
            console('Warehouse.Computer.LegacyUI 1')
            enter('legacy_open')
    elif phase == 'legacy_open' and age > .5:
        assert pawn.get_interaction_target() == computer
        pawn.toggle_carry()
        assert pc.is_warehouse_computer_open() and not unreal.GameplayStatics.is_game_paused(game)
        enter('legacy_capture')
    elif phase == 'legacy_capture' and age > .7:
        capture('Requested%dx%d_Legacy' % RESOLUTIONS[resolution_index])
        pc.close_warehouse_computer()
        console('Warehouse.Computer.LegacyUI 0')
        assert not pc.is_warehouse_computer_open() and not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
        check('Requested %dx%d: dashboard pages and retained legacy rendered at recorded actual dimensions; close restores input' % RESOLUTIONS[resolution_index])
        resolution_index += 1
        if resolution_index < len(RESOLUTIONS):
            console('r.SetRes %dx%dw' % RESOLUTIONS[resolution_index])
            page_index = 0
            enter('reopen')
        else:
            assert pc.get_controlled_pawn() == pawn and pc.get_view_target() == pawn
            request_finish(True, 'Native dashboard/legacy page rendering, real E entry, live state, input return and saved-map preservation passed; visual review required')
    elif phase == 'reopen' and age > 1:
        pawn.toggle_carry()
        assert pc.is_warehouse_computer_open()
        enter('page')


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
        if phase == 'end_play':
            complete(False, detail)
        else:
            request_finish(False, detail)
    finally:
        _busy = False


handle = unreal.register_slate_post_tick_callback(tick)
