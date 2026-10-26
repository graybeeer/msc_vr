"""Scoped official MCP operations for the warehouse cargo lift."""
import json
import runpy
from pathlib import Path
import unreal
import toolset_registry
from toolset_registry.registration import Registration

ROOT = Path(unreal.Paths.project_dir()).resolve()
_registration = None
_test = None

@unreal.uclass()
class ElevatorTools(unreal.ToolsetDefinition):
    """Import, apply, inspect and physically verify this project's cargo elevator."""

    @toolset_registry.tool_call
    @staticmethod
    def apply_model() -> str:
        """Import prepared elevator FBX parts, configure collisions and save the warehouse lift."""
        result = runpy.run_path(str(ROOT/'Scripts/apply_cargo_elevator.py'))
        return json.dumps(result['report'], ensure_ascii=False)

    @toolset_registry.tool_call
    @staticmethod
    def start_transfer_test() -> str:
        """Start an unsaved physical boarding and L1/L2/L3/return test using the saved model and AGV."""
        global _test
        assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
        _test = runpy.run_path(str(ROOT/'Scripts/verify_cargo_elevator_pie.py'))
        return 'Test started. Poll transfer_status; the native world advances between MCP calls.'

    @toolset_registry.tool_call
    @staticmethod
    def transfer_status() -> str:
        """Return current physical elevator validation report without advancing simulation manually."""
        path=ROOT/'Saved/Testing/CargoElevator/TransferReport.json'
        return path.read_text(encoding='utf-8') if path.exists() else '{}'

    @toolset_registry.tool_call
    @staticmethod
    def inspect_lift() -> str:
        """Return saved-world lift components and any unsaved editor assets."""
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        lifts=[a for a in actors if isinstance(a,unreal.WarehouseElevator)]
        return json.dumps([{'actor':a.get_path_name(),'label':a.get_actor_label(),
            'position':str(a.get_actor_location()),'model':a.get_editor_property('use_cargo_model'),
            'parts':[{'name':c.get_name(),'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,
                'collision':str(c.get_collision_enabled()),'scale':str(c.get_editor_property('relative_scale3d'))}
                for c in a.get_components_by_class(unreal.StaticMeshComponent)]} for a in lifts])

    @toolset_registry.tool_call
    @staticmethod
    def close_editor() -> str:
        """Close the editor after the caller has saved work, for loading a new native build."""
        unreal.SystemLibrary.quit_editor()
        return 'Editor shutdown requested'

def register():
    global _registration
    _registration = Registration([ElevatorTools])
    _registration.register()
