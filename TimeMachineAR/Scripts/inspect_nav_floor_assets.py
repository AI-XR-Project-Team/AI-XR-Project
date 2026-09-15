"""Read-only dump of the floor-guide material and textures (what the .uassets really contain).

    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi

Prints NAV_FLOOR_AUDIT lines: material blend/shading mode + expression graph, and each
texture's size / format / sRGB / mips / LOD group. Used before touching the assets so the
material contract (parameter name, blend mode) is confirmed from the asset, not from comments.
"""
import unreal

FLOOR_DIRS = ["/Game/UI/Nav/Floor", "/Game/UI/Nav/Floor/Golden"]
MATERIALS = ["/Game/UI/Nav/Floor/M_NavFloorArrow", "/Game/UI/Nav/Floor/Golden/M_NavFootprintGolden"]


def dump_material(path):
    mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if mat is None:
        unreal.log("NAV_FLOOR_AUDIT material MISSING %s" % path)
        return
    unreal.log("NAV_FLOOR_AUDIT material %s blend=%s shading=%s twosided=%s" % (
        path, mat.get_editor_property("blend_mode"), mat.get_editor_property("shading_model"),
        mat.get_editor_property("two_sided")))
    for name in ("FootprintTex", "Tint", "Opacity", "BreathAmp", "BreathPeriod"):
        try:
            unreal.log("NAV_FLOOR_AUDIT   param %s tex=%s scalar=%s vec=%s" % (
                name,
                unreal.MaterialEditingLibrary.get_material_default_texture_parameter_value(mat, name),
                unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(mat, name),
                unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(mat, name)))
        except Exception as exc:  # noqa: BLE001
            unreal.log("NAV_FLOOR_AUDIT   param %s ? (%s)" % (name, exc))
    try:
        for expr in mat.get_editor_property("expressions"):
            desc = expr.get_class().get_name()
            for prop in ("parameter_name", "texture", "default_value", "r", "period"):
                try:
                    desc += " %s=%s" % (prop, expr.get_editor_property(prop))
                except Exception:  # noqa: BLE001
                    pass
            unreal.log("NAV_FLOOR_AUDIT   expr %s" % desc)
    except Exception as exc:  # noqa: BLE001
        unreal.log("NAV_FLOOR_AUDIT   expressions unavailable (%s)" % exc)


def dump_textures(folder):
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.log("NAV_FLOOR_AUDIT dir MISSING %s" % folder)
        return
    for asset_path in sorted(unreal.EditorAssetLibrary.list_assets(folder, recursive=False)):
        tex = unreal.load_asset(asset_path)
        if not isinstance(tex, unreal.Texture2D):
            unreal.log("NAV_FLOOR_AUDIT asset %s (%s)" % (asset_path, tex.get_class().get_name() if tex else "?"))
            continue
        unreal.log("NAV_FLOOR_AUDIT texture %s %dx%d srgb=%s comp=%s mips=%s group=%s stream=%s" % (
            asset_path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
            tex.get_editor_property("srgb"), tex.get_editor_property("compression_settings"),
            tex.get_editor_property("mip_gen_settings"), tex.get_editor_property("lod_group"),
            not tex.get_editor_property("never_stream")))


for m in MATERIALS:
    dump_material(m)
for d in FLOOR_DIRS:
    dump_textures(d)
unreal.log("NAV_FLOOR_AUDIT_DONE")
