"""Project-specific MCP tools; no scene edits or simulation start on editor launch."""
import unreal
try:
    import warehouse_elevator_tools
    warehouse_elevator_tools.register()
except Exception as error:
    unreal.log_warning('Cargo elevator MCP registration: ' + str(error))
