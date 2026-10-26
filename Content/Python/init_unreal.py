"""Register optional project MCP tools; never change the level at startup."""
import importlib.util
import unreal
if importlib.util.find_spec('toolset_registry') is not None:
    try:
        import warehouse_elevator_tools
        warehouse_elevator_tools.register()
    except Exception as error:
        unreal.log_warning('Elevator MCP registration: '+str(error))
