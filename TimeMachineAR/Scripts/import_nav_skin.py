"""Run with UE 5.4 Python: imports the navigation skin without touching Blueprint logic."""
from pathlib import Path
import unreal

source = Path(unreal.Paths.project_dir()).resolve().parent / "docs/ai-handoff/inbox/sheet"
destination = "/Game/UI/Nav/Skin"
files = sorted(source.glob("*.png"))
if not files:
    raise RuntimeError("Navigation PNGs missing: " + str(source))
tasks = []
for path in files:
    task = unreal.AssetImportTask()
    task.filename = str(path)
    task.destination_path = destination
    task.destination_name = path.stem
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for path in files:
    texture = unreal.load_asset(destination + "/" + path.stem)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Import failed: " + path.stem)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
# Runtime code loads by path. Keep these assets in packaged Android builds too.
label_path = destination + "/NavSkinCookLabel"
label = unreal.load_asset(label_path) if unreal.EditorAssetLibrary.does_asset_exist(label_path) else None
if label is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
    label = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "NavSkinCookLabel", destination, unreal.PrimaryAssetLabel, factory)
label.set_editor_property("label_assets_in_my_directory", True)
rules = label.get_editor_property("rules")
rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules", rules)
unreal.EditorAssetLibrary.save_loaded_asset(label)
unreal.log("NAV_SKIN_IMPORT_OK: %d textures and cook label" % len(files))
