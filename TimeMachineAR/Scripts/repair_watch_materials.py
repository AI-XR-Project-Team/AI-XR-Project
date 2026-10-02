"""Repair entry point: run editor automation TimeMachineAR.Reveal.RepairWatchMaterials.
The normal import script now emits SAMPLERTYPE_MASKS correctly; this repair avoids reimporting meshes.
"""
import unreal
unreal.SystemLibrary.execute_console_command(None, 'Automation RunTests TimeMachineAR.Reveal.RepairWatchMaterials')
