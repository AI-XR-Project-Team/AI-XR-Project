"""Run with UE 5.4 Python: imports the golden footprint set and builds its material.

    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
    (or Tools > Execute Python Script inside the editor)

Source PNGs : docs/ai-handoff/inbox/nav_footprints/*.png   (Scripts/extract_nav_footprints.py output)
Destination : /Game/UI/Nav/Floor/Golden
  T_Footprint_Left / T_Footprint_Right / T_NavArrivalRing / T_Footprint_Glow
        world textures, sRGB, TC_Default (ASTC RGBA on Android), mips from group, streaming
        -- they lie on the floor and get viewed from 0.5 m to 6 m, so mips are needed
        (UI / NoMipmaps would shimmer at distance).
  T_NavForwardChevron
        HUD-only (Slate brush) -> UI group, no mips, never stream.
  M_NavFootprintGolden
        Unlit + Translucent + TwoSided. Params: FootprintTex (Texture), Tint (Vector),
        Opacity / BreathAmp / BreathPeriod (Scalar).
          Emissive = FootprintTex.rgb * Tint
          Opacity  = FootprintTex.a * Opacity * (1 + BreathAmp * sin(2*pi*Time / BreathPeriod))
        Same FootprintTex contract as the old M_NavFloorArrow so the actor can fall back to it.
  NavFloorGoldenCookLabel
        PrimaryAssetLabel (AlwaysCook) for path-loaded assets, on top of DefaultGame.ini's
        DirectoriesToAlwaysCook=/Game/UI. Verify in the staged pak list anyway.
Blueprint logic is not touched.
"""
from pathlib import Path

import unreal

SOURCE = Path(unreal.Paths.project_dir()).resolve().parent / "docs/ai-handoff/inbox/nav_footprints"
DEST = "/Game/UI/Nav/Floor/Golden"
WORLD_TEXTURES = ["T_Footprint_Left", "T_Footprint_Right", "T_NavArrivalRing", "T_Footprint_Glow"]
UI_TEXTURES = ["T_NavForwardChevron"]
MATERIAL = "M_NavFootprintGolden"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def import_textures():
    tasks = []
    for name in WORLD_TEXTURES + UI_TEXTURES:
        path = SOURCE / (name + ".png")
        if not path.exists():
            raise RuntimeError("Footprint PNG missing: " + str(path))
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = DEST
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    tools.import_asset_tasks(tasks)

    for name in WORLD_TEXTURES + UI_TEXTURES:
        tex = unreal.load_asset(DEST + "/" + name)
        if not isinstance(tex, unreal.Texture2D):
            raise RuntimeError("Import failed: " + name)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        if name in UI_TEXTURES:
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            tex.set_editor_property("never_stream", True)
        else:
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
            tex.set_editor_property("never_stream", False)
        eal.save_loaded_asset(tex)
        unreal.log("NAV_FOOTPRINT_IMPORT texture %s %dx%d" % (
            name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))


def build_material():
    mat_path = DEST + "/" + MATERIAL
    if eal.does_asset_exist(mat_path):
        eal.delete_asset(mat_path)   # rebuilt from scratch every run -> deterministic graph
    mat = tools.create_asset(MATERIAL, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, -100)
    tex.set_editor_property("parameter_name", "FootprintTex")
    tex.set_editor_property("texture", unreal.load_asset(DEST + "/T_Footprint_Left"))

    tint = node(unreal.MaterialExpressionVectorParameter, -900, 200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    emissive = node(unreal.MaterialExpressionMultiply, -500, 0)
    mel.connect_material_expressions(tex, "RGB", emissive, "A")
    mel.connect_material_expressions(tint, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = node(unreal.MaterialExpressionScalarParameter, -900, 400)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)

    amp = node(unreal.MaterialExpressionScalarParameter, -900, 550)
    amp.set_editor_property("parameter_name", "BreathAmp")
    amp.set_editor_property("default_value", 0.0)

    period = node(unreal.MaterialExpressionScalarParameter, -900, 700)
    period.set_editor_property("parameter_name", "BreathPeriod")
    period.set_editor_property("default_value", 1.8)

    time = node(unreal.MaterialExpressionTime, -900, 850)
    phase = node(unreal.MaterialExpressionDivide, -700, 800)      # Time / Period
    mel.connect_material_expressions(time, "", phase, "A")
    mel.connect_material_expressions(period, "", phase, "B")
    sine = node(unreal.MaterialExpressionSine, -550, 800)          # Period 1 -> sin(2*pi*x)
    mel.connect_material_expressions(phase, "", sine, "")
    swing = node(unreal.MaterialExpressionMultiply, -400, 700)     # sin * BreathAmp
    mel.connect_material_expressions(sine, "", swing, "A")
    mel.connect_material_expressions(amp, "", swing, "B")
    one = node(unreal.MaterialExpressionConstant, -400, 600)
    one.set_editor_property("r", 1.0)
    breath = node(unreal.MaterialExpressionAdd, -250, 650)         # 1 + sin * BreathAmp
    mel.connect_material_expressions(one, "", breath, "A")
    mel.connect_material_expressions(swing, "", breath, "B")

    base_alpha = node(unreal.MaterialExpressionMultiply, -500, 400)   # A * Opacity
    mel.connect_material_expressions(tex, "A", base_alpha, "A")
    mel.connect_material_expressions(opacity, "", base_alpha, "B")
    final_alpha = node(unreal.MaterialExpressionMultiply, -100, 450)  # * breath
    mel.connect_material_expressions(base_alpha, "", final_alpha, "A")
    mel.connect_material_expressions(breath, "", final_alpha, "B")
    mel.connect_material_property(final_alpha, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("NAV_FOOTPRINT_IMPORT material %s blend=%s shading=%s" % (
        mat_path, mat.get_editor_property("blend_mode"), mat.get_editor_property("shading_model")))


def ensure_cook_label():
    label_path = DEST + "/NavFloorGoldenCookLabel"
    label = unreal.load_asset(label_path) if eal.does_asset_exist(label_path) else None
    if label is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
        label = tools.create_asset("NavFloorGoldenCookLabel", DEST, unreal.PrimaryAssetLabel, factory)
    label.set_editor_property("label_assets_in_my_directory", True)
    rules = label.get_editor_property("rules")
    rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    eal.save_loaded_asset(label)


import_textures()
build_material()
ensure_cook_label()
unreal.log("NAV_FOOTPRINT_IMPORT_OK: %d textures, material, cook label" % (len(WORLD_TEXTURES) + len(UI_TEXTURES)))
