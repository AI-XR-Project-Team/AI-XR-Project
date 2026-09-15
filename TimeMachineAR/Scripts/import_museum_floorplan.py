"""Import the user's original UI artwork losslessly for runtime UMG region brushes."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
task = unreal.AssetImportTask()
task.filename = str(project / "Asset/NavReference/T_MuseumFloorplan.png")
task.destination_path = "/Game/UI/Nav/Reference"
task.destination_name = "T_MuseumFloorplan"
task.automated = True
task.replace_existing = True
task.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.load_asset("/Game/UI/Nav/Reference/T_MuseumFloorplan")
if not isinstance(texture, unreal.Texture2D):
    raise RuntimeError("Museum reference import failed")
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("never_stream", True)
texture.set_editor_property("srgb", True)
unreal.EditorAssetLibrary.save_loaded_asset(texture)
unreal.log("MUSEUM_REFERENCE_IMPORT_OK")
