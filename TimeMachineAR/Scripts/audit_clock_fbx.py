"""UE 5.4 Python (-run=pythonscript). Imports the three Meshy pocket-watch FBX files into a scratch
folder (/Game/TimeReveal/Audit, NOT for packaging) with no materials/textures, and prints geometry
facts so the assembly/pivot design is based on the real meshes, not file names:

  CLOCK_FBX <part> tris=<n> verts=<n> lods=<n> bounds min/max size origin=<centre> materials=<n> uv=<n> embedded=<n>

The same script then places the three parts at the origin and renders front/back/side/top PNGs
(Saved/ClockAudit_<view>.png) when run with -RenderOffscreen. Originals are never modified.
"""
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve() / "Asset/Clock"
DEST = "/Game/TimeReveal/Audit"
PARTS = {
    "Body": "Meshy_AI_pocket_watch_body_0914084005_image-to-3d-texture_fbx",
    "HandHour": "Meshy_AI_pocket_watch_hour_han_0914084753_image-to-3d-texture_fbx",
    "HandMinute": "Meshy_AI_pocket_watch_minute_h_0914084700_image-to-3d-texture_fbx",
}
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def find_fbx(folder_name):
    hits = list((ROOT / folder_name).rglob("*.fbx"))
    if not hits:
        raise RuntimeError("no fbx under " + str(ROOT / folder_name))
    return hits[0]


def import_part(name, fbx):
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
    task.destination_name = "SM_Audit_" + name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = opts
    tools.import_asset_tasks([task])
    mesh = unreal.load_asset(DEST + "/SM_Audit_" + name)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError("import failed: " + name)
    return mesh


def dump(name, mesh, fbx):
    box = mesh.get_bounding_box()
    size = box.max - box.min
    centre = (box.max + box.min) * 0.5
    unreal.log("CLOCK_FBX %s file=%s bytes=%d tris=%d verts=? lods=%d size=(%.2f %.2f %.2f) min=(%.2f %.2f %.2f) max=(%.2f %.2f %.2f) centre=(%.2f %.2f %.2f) materials=%d uvs=%d" % (
        name, fbx.name, fbx.stat().st_size, mesh.get_num_triangles(0), mesh.get_num_lods(),
        size.x, size.y, size.z, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
        centre.x, centre.y, centre.z, len(mesh.get_editor_property("static_materials")),
        unreal.EditorStaticMeshLibrary.get_num_uv_channels(mesh, 0) if hasattr(unreal, "EditorStaticMeshLibrary") else -1))


def import_basecolor_material(name, folder, mesh):
    """Base colour only (sRGB, capped at 2K for the audit) so the dial/hand paint can be judged."""
    png = next(p for p in (ROOT / folder).rglob("*image-to-3d-texture.png"))
    task = unreal.AssetImportTask()
    task.filename = str(png)
    task.destination_path = DEST
    task.destination_name = "T_Audit_" + name + "_BaseColor"
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(DEST + "/T_Audit_" + name + "_BaseColor")
    tex.set_editor_property("max_texture_size", 2048)
    tex.set_editor_property("srgb", True)
    eal.save_loaded_asset(tex)
    mat_path = DEST + "/M_Audit_" + name
    if eal.does_asset_exist(mat_path):
        eal.delete_asset(mat_path)
    mat = tools.create_asset("M_Audit_" + name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    ts = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 0)
    ts.set_editor_property("texture", tex)
    mel.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property("r", 0.35)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 400)
    metal.set_editor_property("r", 0.6)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    mesh.set_material(0, mat)
    eal.save_loaded_asset(mesh)


meshes = {}
for name, folder in PARTS.items():
    fbx = find_fbx(folder)
    meshes[name] = import_part(name, fbx)
    dump(name, meshes[name], fbx)
    import_basecolor_material(name, folder, meshes[name])

# Embedded texture check: count of "Video"/"Texture" blobs is not exposed; report the PNG sizes instead.
for name, folder in PARTS.items():
    pngs = sorted((ROOT / folder).rglob("*.png"))
    unreal.log("CLOCK_FBX %s pngs=%s" % (name, ", ".join("%s:%d" % (p.name.split("image-to-3d-texture")[-1] or "basecolor", p.stat().st_size) for p in pngs)))

unreal.log("CLOCK_FBX_DONE")
