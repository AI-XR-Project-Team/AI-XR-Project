"""UE 5.4 Python (-run=pythonscript). Builds the mobile pocket-watch assets used by ATimeWatchActor
(design.md §3.1), re-runnable (replace_existing everywhere).

Steps:
  1. Import the 3 Meshy FBX into /Game/TimeReveal/Clock/ as SM_Watch_Body / SM_Watch_HandHour /
     SM_Watch_HandMinute — no materials/textures, CombineMeshes, no Nanite, no lightmap UVs,
     no auto collision (the watch never collides).
  2. Reduce LOD0 itself in place (percent_triangles ~0.4% body / 0.08% hour / 0.14% minute) via
     UStaticMeshEditorSubsystem.SetLods (the EditorStaticMeshLibrary equivalent used in UE<5.0
     tutorials is fully UE_DEPRECATED in 5.4 -> this is its replacement, see
     Engine/Source/Editor/StaticMeshEditor/Public/StaticMeshEditorSubsystemHelpers.h).
  3. Import the 9 derived PNGs from Scripts/derive_clock_textures.py (docs/ai-handoff/inbox/clock/)
     as textures: BaseColor sRGB/TC_Default, Normal TC_Normalmap/sRGB off, MR TC_Masks/sRGB off.
  4. Build M_Watch_Body (bound directly to the body's own 3 textures) and M_Watch_Hand (texture
     PARAMETERS so MI_Watch_HandHour / MI_Watch_HandMinute can each point at their own hand's
     textures while sharing one graph) — DefaultLit, BaseColor*Tint, Normal, Metallic=MR.R,
     Roughness=MR.G, Emissive=BaseColor*Tint*GoldGlow (GoldGlow default 0, Tint default white).
  5. Assign materials/instances to the 3 meshes' slot 0.
  6. Create PrimaryAssetLabel TimeRevealClockCookLabel (AlwaysCook) so `/Game/TimeReveal/Clock`
     ships even though DefaultGame.ini's DirectoriesToAlwaysCook only covers /Game/UI.

Never touches the original FBX/PNG under TimeMachineAR/Asset/Clock/ (read-only) or the audit-only
/Game/TimeReveal/Audit assets from Scripts/audit_clock_fbx.py.
"""
from pathlib import Path

import unreal

# UStaticMeshEditorSubsystem (5.0+ reduction API) lives in the "StaticMeshEditor" module, which is
# NOT auto-loaded by a headless `-run=pythonscript` commandlet (no Static Mesh Editor UI ever opens
# to pull it in) -- without this, GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() returns
# null and EditorStaticMeshLibrary.set_lods() silently no-ops (returns -1, LOD0 stays unreduced).
unreal.load_module("StaticMeshEditor")

PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
SOURCE_ROOT = PROJECT_ROOT / "Asset/Clock"
TEXTURE_SOURCE = PROJECT_ROOT.parent / "docs/ai-handoff/inbox/clock"
DEST = "/Game/TimeReveal/Clock"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# name -> (fbx folder, dest mesh name, LOD0 percent_triangles, texture prefix)
PARTS = {
    "Body": (
        "Meshy_AI_pocket_watch_body_0914084005_image-to-3d-texture_fbx",
        "SM_Watch_Body", 0.05, "Watch_Body",
    ),
    "HandHour": (
        "Meshy_AI_pocket_watch_hour_han_0914084753_image-to-3d-texture_fbx",
        "SM_Watch_HandHour", 0.0008, "Watch_HandHour",
    ),
    "HandMinute": (
        "Meshy_AI_pocket_watch_minute_h_0914084700_image-to-3d-texture_fbx",
        "SM_Watch_HandMinute", 0.0014, "Watch_HandMinute",
    ),
}


def find_fbx(folder_name: str) -> Path:
    hits = sorted((SOURCE_ROOT / folder_name).rglob("*.fbx"))
    if len(hits) != 1:
        raise RuntimeError("no fbx under " + str(SOURCE_ROOT / folder_name))
    return hits[0]


# --------------------------------------------------------------------- 1. FBX import


def import_mesh(mesh_name: str, fbx: Path) -> unreal.StaticMesh:
    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_as_skeletal", False)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("import_animations", False)
    opts.set_editor_property("create_physics_asset", False)
    sm = opts.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("build_nanite", False)

    task = unreal.AssetImportTask()
    task.filename = str(fbx)
    task.destination_path = DEST
    task.destination_name = mesh_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = opts
    tools.import_asset_tasks([task])

    mesh = unreal.load_asset(DEST + "/" + mesh_name)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError("mesh import failed: " + mesh_name)
    return mesh


# --------------------------------------------------------------------- 2. LOD0 reduction


def reduce_lod0(mesh: unreal.StaticMesh, percent_triangles: float) -> int:
    before = mesh.get_num_triangles(0)
    # unreal.EditorStaticMeshLibrary is UE_DEPRECATED in C++ (compiler warning only, on the class);
    # its python-exposed set_lods(mesh, FStaticMeshReductionOptions) overload still forwards straight
    # to UStaticMeshEditorSubsystem::SetLods, which replaces LOD0's own ReductionSettings (not a new
    # LOD) when given a single-entry array -- exactly "LOD0 자체를 축소" from design.md §3.1.
    settings = unreal.StaticMeshReductionSettings(percent_triangles=percent_triangles, screen_size=1.0)
    options = unreal.StaticMeshReductionOptions(reduction_settings=[settings])
    ret = unreal.EditorStaticMeshLibrary.set_lods(mesh, options)
    if ret < 0:
        raise RuntimeError("set_lods failed for %s (ret=%d) -- StaticMeshEditorSubsystem missing?" % (mesh.get_name(), ret))
    eal.save_loaded_asset(mesh)
    after = mesh.get_num_triangles(0)
    unreal.log("WATCH_IMPORT reduce %s before=%d after=%d percent=%.4f" % (mesh.get_name(), before, after, percent_triangles))
    return after


# --------------------------------------------------------------------- 3. Texture import


def import_texture(name: str, colour: bool, normal: bool) -> unreal.Texture2D:
    png = TEXTURE_SOURCE / (name + ".png")
    if not png.exists():
        raise RuntimeError("derived texture missing (run Scripts/derive_clock_textures.py first): " + str(png))
    task = unreal.AssetImportTask()
    task.filename = str(png)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(DEST + "/" + name)
    if not isinstance(tex, unreal.Texture2D):
        raise RuntimeError("texture import failed: " + name)
    if normal:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif colour:
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    else:
        # MR mask: linear, packed R=metallic G=roughness.
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    unreal.log("WATCH_IMPORT texture %s %dx%d srgb=%s" % (
        name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(), tex.get_editor_property("srgb")))
    return tex


def import_texture_set(prefix: str):
    base = import_texture("T_%s_BaseColor" % prefix, colour=True, normal=False)
    normal = import_texture("T_%s_Normal" % prefix, colour=False, normal=True)
    mr = import_texture("T_%s_MR" % prefix, colour=False, normal=False)
    return base, normal, mr


# --------------------------------------------------------------------- 4. Materials
# 그래프는 두 재질(Body/Hand) 공용: BaseColor = BaseColorTex.RGB * Tint, Normal = NormalTex.RGB,
# Metallic = MRTex.R, Roughness = MRTex.G, Emissive = (BaseColorTex.RGB*Tint) * GoldGlow.
# GoldGlow 는 전체 Emissive 대체가 아니라 어두운 배경에서 금속이 죽을 때 켜는 보조 표현(§3.1).


def build_material(mat_name: str, default_base: unreal.Texture2D, default_normal: unreal.Texture2D,
                    default_mr: unreal.Texture2D) -> unreal.Material:
    mat_path = DEST + "/" + mat_name
    mat = unreal.load_asset(mat_path) if eal.does_asset_exist(mat_path) else tools.create_asset(mat_name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    base_tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, -300)
    base_tex.set_editor_property("parameter_name", "BaseColorTex")
    base_tex.set_editor_property("texture", default_base)
    base_tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    normal_tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    normal_tex.set_editor_property("parameter_name", "NormalTex")
    normal_tex.set_editor_property("texture", default_normal)
    normal_tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)

    mr_tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, 300)
    mr_tex.set_editor_property("parameter_name", "MRTex")
    mr_tex.set_editor_property("texture", default_mr)
    mr_tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)

    tint = node(unreal.MaterialExpressionVectorParameter, -700, -500)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    glow = node(unreal.MaterialExpressionScalarParameter, -700, 650)
    glow.set_editor_property("parameter_name", "GoldGlow")
    glow.set_editor_property("default_value", 0.0)

    # BaseColor = BaseColorTex.RGB * Tint
    tinted_base = node(unreal.MaterialExpressionMultiply, -350, -400)
    mel.connect_material_expressions(base_tex, "RGB", tinted_base, "A")
    mel.connect_material_expressions(tint, "", tinted_base, "B")
    mel.connect_material_property(tinted_base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Normal = NormalTex.RGB
    mel.connect_material_property(normal_tex, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # Metallic = MR.R, Roughness = MR.G
    mel.connect_material_property(mr_tex, "R", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(mr_tex, "G", unreal.MaterialProperty.MP_ROUGHNESS)

    # Emissive = tinted BaseColor * GoldGlow (0 이면 완전히 꺼짐 = 기존 DefaultLit 그대로).
    emissive = node(unreal.MaterialExpressionMultiply, -350, 650)
    mel.connect_material_expressions(tinted_base, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("WATCH_IMPORT material %s" % mat_path)
    return mat


def build_material_instance(mi_name: str, parent: unreal.Material, base: unreal.Texture2D,
                             normal: unreal.Texture2D, mr: unreal.Texture2D) -> unreal.MaterialInstanceConstant:
    mi_path = DEST + "/" + mi_name
    factory = unreal.MaterialInstanceConstantFactoryNew()
    mi = unreal.load_asset(mi_path) if eal.does_asset_exist(mi_path) else tools.create_asset(mi_name, DEST, unreal.MaterialInstanceConstant, factory)
    mi.set_editor_property("parent", parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseColorTex", base)
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", normal)
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    eal.save_loaded_asset(mi)
    unreal.log("WATCH_IMPORT material_instance %s parent=%s" % (mi_path, parent.get_name()))
    return mi


# --------------------------------------------------------------------- 5. Cook label


def ensure_cook_label():
    label_path = DEST + "/TimeRevealClockCookLabel"
    label = unreal.load_asset(label_path) if eal.does_asset_exist(label_path) else None
    if label is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
        label = tools.create_asset("TimeRevealClockCookLabel", DEST, unreal.PrimaryAssetLabel, factory)
    label.set_editor_property("label_assets_in_my_directory", True)
    rules = label.get_editor_property("rules")
    rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    eal.save_loaded_asset(label)
    unreal.log("WATCH_IMPORT cook_label %s" % label_path)


# --------------------------------------------------------------------- main


def main():
    meshes = {}
    for part, (folder, mesh_name, pct, _tex_prefix) in PARTS.items():
        fbx = find_fbx(folder)
        mesh = import_mesh(mesh_name, fbx)
        meshes[part] = mesh
        reduce_lod0(mesh, pct)

    textures = {}
    for part, (_folder, _mesh_name, _pct, tex_prefix) in PARTS.items():
        textures[part] = import_texture_set(tex_prefix)

    body_base, body_normal, body_mr = textures["Body"]
    hour_base, hour_normal, hour_mr = textures["HandHour"]
    minute_base, minute_normal, minute_mr = textures["HandMinute"]

    m_body = build_material("M_Watch_Body", body_base, body_normal, body_mr)
    m_hand = build_material("M_Watch_Hand", hour_base, hour_normal, hour_mr)

    mi_hour = build_material_instance("MI_Watch_HandHour", m_hand, hour_base, hour_normal, hour_mr)
    mi_minute = build_material_instance("MI_Watch_HandMinute", m_hand, minute_base, minute_normal, minute_mr)

    meshes["Body"].set_material(0, m_body)
    meshes["HandHour"].set_material(0, mi_hour)
    meshes["HandMinute"].set_material(0, mi_minute)
    for part in ("Body", "HandHour", "HandMinute"):
        eal.save_loaded_asset(meshes[part])

    ensure_cook_label()

    material_count = 2 + 2  # M_Watch_Body, M_Watch_Hand, MI_Watch_HandHour, MI_Watch_HandMinute
    unreal.log("WATCH_IMPORT_OK tris body=%d hour=%d minute=%d textures=9 materials=%d" % (
        meshes["Body"].get_num_triangles(0), meshes["HandHour"].get_num_triangles(0),
        meshes["HandMinute"].get_num_triangles(0), material_count))


main()

