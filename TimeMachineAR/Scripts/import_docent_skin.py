"""UE 5.4 Python 으로 실행. 도슨트 채팅 스킨 텍스처를 임포트한다.

import_nav_skin.py 와 같은 레시피다. 런타임 코드가 경로 문자열로 LoadObject
하므로 쿠커가 참조를 못 찾는다. ALWAYS_COOK 라벨로 디렉터리를 쿡에 묶는다.
"""
from pathlib import Path
import unreal

source = Path(unreal.Paths.project_dir()).resolve().parent / "docs/ai-handoff/inbox/docent_sheet"
destination = "/Game/UI/Docent/Skin"
files = sorted(p for p in source.glob("*.png"))   # ref/ 하위는 제외
if not files:
    raise RuntimeError("Docent PNGs missing: " + str(source))

tasks = []
for path in files:
    t = unreal.AssetImportTask()
    t.filename = str(path)
    t.destination_path = destination
    t.destination_name = path.stem
    t.automated = True
    t.replace_existing = True
    t.save = False
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for path in files:
    tex = unreal.load_asset(destination + "/" + path.stem)
    if not isinstance(tex, unreal.Texture2D):
        raise RuntimeError("Import failed: " + path.stem)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)

label_path = destination + "/DocentSkinCookLabel"
label = unreal.load_asset(label_path) if unreal.EditorAssetLibrary.does_asset_exist(label_path) else None
if label is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
    label = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DocentSkinCookLabel", destination, unreal.PrimaryAssetLabel, factory)
label.set_editor_property("label_assets_in_my_directory", True)
rules = label.get_editor_property("rules")
rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules", rules)
unreal.EditorAssetLibrary.save_loaded_asset(label)
unreal.log("DOCENT_SKIN_IMPORT_OK: %d textures and cook label" % len(files))
