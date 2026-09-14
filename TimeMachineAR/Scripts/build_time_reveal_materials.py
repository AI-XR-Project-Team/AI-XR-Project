"""UE 5.4 Python (-run=pythonscript). Builds the time-reveal support materials from code (re-runnable):

  /Game/TimeReveal/FX/M_TimeFX_Sprite   Unlit + Additive sprite material for the derived Niagara systems.
        Params: PrimaryColor (Vector, #4BA3FF), SecondaryColor (Vector, #D9AA57), SecondaryMix (Scalar 0),
                Intensity (Scalar 2.0), Fade (Scalar 1.0), UseParticleColor (Scalar 0).
        Emissive = lerp(1, ParticleColor.rgb, UseParticleColor) * lerp(Primary, Secondary, SecondaryMix)
                   * Intensity * Fade * RadialMask(UV) * ParticleColor.a
        No texture — the soft dot is (1 - saturate(length((UV-0.5)*2)))^2, so nothing extra to cook.
        The Niagara sprite renderers consume it through the User.SpriteMaterial binding (BuildTimeRevealFX test).
  /Game/TimeReveal/T_TimeReveal_Noise      256x256 tileable value noise (docs/ai-handoff/inbox/time_reveal/T_TimeReveal_Noise.png).
  /Game/TimeReveal/M_ArchelonReveal        Duplicate of /Game/Stuff/Asset/Archelon/Archelon_Material switched to Masked:
        OpacityMask = 0.3333 + (Dissolve - Noise(UV*NoiseTiling))   -> visible where Noise <= Dissolve.
        Emissive   += EdgeColor * EdgeStrength * band(|Dissolve - Noise| < EdgeWidth)   (blue-white time-energy rim).
        Params: Dissolve (Scalar 1 = fully visible), NoiseTiling (6), EdgeWidth (0.06), EdgeStrength (3), EdgeColor (#DDF5FF).
        The shared original material is not modified; the runtime swaps only slot 0 of the flesh mesh to a MID of this
        material during Revealing and restores the original at Complete.
  /Game/TimeReveal/TimeRevealCookLabel     PrimaryAssetLabel AlwaysCook for /Game/TimeReveal (non-recursive dirs get
        their own labels: Clock/ (Sonnet 1), FX/).
"""
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve().parent
NOISE_PNG = ROOT / "docs/ai-handoff/inbox/time_reveal/T_TimeReveal_Noise.png"
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def srgb_to_linear(hexstr):
    c = [int(hexstr[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    def f(v):
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(f(c[0]), f(c[1]), f(c[2]), 1.0)


def fresh_material(name, folder):
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())


def scalar(mat, name, default, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


def vector(mat, name, default, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


def build_sprite_material():
    mat = fresh_material("M_TimeFX_Sprite", "/Game/TimeReveal/FX")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)
    mat.set_editor_property("used_with_particle_sprites", True)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    pcol = node(unreal.MaterialExpressionParticleColor, -1200, -200)
    prim = vector(mat, "PrimaryColor", srgb_to_linear("4BA3FF"), -1200, 100)
    sec = vector(mat, "SecondaryColor", srgb_to_linear("D9AA57"), -1200, 300)
    mix = scalar(mat, "SecondaryMix", 0.0, -1200, 500)
    inten = scalar(mat, "Intensity", 2.0, -1200, 620)
    fade = scalar(mat, "Fade", 1.0, -1200, 740)

    lerp = node(unreal.MaterialExpressionLinearInterpolate, -900, 250)
    mel.connect_material_expressions(prim, "", lerp, "A")
    mel.connect_material_expressions(sec, "", lerp, "B")
    mel.connect_material_expressions(mix, "", lerp, "Alpha")

    # rgb = lerp(1, ParticleColor.rgb, UseParticleColor): 원본의 ColorFromCurve 색조를 무시하고(기본 0)
    # 우리 팔레트만 쓴다. 커브 색을 살리고 싶으면 1.
    usepc = scalar(mat, "UseParticleColor", 0.0, -1200, -50)
    one3 = node(unreal.MaterialExpressionConstant3Vector, -1200, -350)
    one3.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    pmix = node(unreal.MaterialExpressionLinearInterpolate, -950, -150)
    mel.connect_material_expressions(one3, "", pmix, "A")
    mel.connect_material_expressions(pcol, "RGB", pmix, "B")
    mel.connect_material_expressions(usepc, "", pmix, "Alpha")
    tint = node(unreal.MaterialExpressionMultiply, -700, 0)          # rgb * tint
    mel.connect_material_expressions(pmix, "", tint, "A")
    mel.connect_material_expressions(lerp, "", tint, "B")
    m1 = node(unreal.MaterialExpressionMultiply, -550, 100)          # * Intensity
    mel.connect_material_expressions(tint, "", m1, "A")
    mel.connect_material_expressions(inten, "", m1, "B")
    m2 = node(unreal.MaterialExpressionMultiply, -400, 200)          # * Fade
    mel.connect_material_expressions(m1, "", m2, "A")
    mel.connect_material_expressions(fade, "", m2, "B")

    # Radial soft dot from UVs: (1 - saturate(length((uv-0.5)*2)))^2
    uv = node(unreal.MaterialExpressionTextureCoordinate, -1200, 900)
    half = node(unreal.MaterialExpressionConstant2Vector, -1200, 1020)
    half.set_editor_property("r", 0.5)
    half.set_editor_property("g", 0.5)
    sub = node(unreal.MaterialExpressionSubtract, -1000, 950)
    mel.connect_material_expressions(uv, "", sub, "A")
    mel.connect_material_expressions(half, "", sub, "B")
    two = node(unreal.MaterialExpressionConstant, -1000, 1080)
    two.set_editor_property("r", 2.0)
    scaled = node(unreal.MaterialExpressionMultiply, -850, 980)
    mel.connect_material_expressions(sub, "", scaled, "A")
    mel.connect_material_expressions(two, "", scaled, "B")
    dist = node(unreal.MaterialExpressionDistance, -700, 980)
    zero = node(unreal.MaterialExpressionConstant2Vector, -850, 1120)
    mel.connect_material_expressions(scaled, "", dist, "A")
    mel.connect_material_expressions(zero, "", dist, "B")
    om = node(unreal.MaterialExpressionOneMinus, -550, 980)
    mel.connect_material_expressions(dist, "", om, "")
    sat = node(unreal.MaterialExpressionSaturate, -420, 980)
    mel.connect_material_expressions(om, "", sat, "")
    sq = node(unreal.MaterialExpressionMultiply, -300, 980)
    mel.connect_material_expressions(sat, "", sq, "A")
    mel.connect_material_expressions(sat, "", sq, "B")
    alpha = node(unreal.MaterialExpressionMultiply, -150, 900)       # mask * ParticleColor.a
    mel.connect_material_expressions(sq, "", alpha, "A")
    mel.connect_material_expressions(pcol, "A", alpha, "B")

    final = node(unreal.MaterialExpressionMultiply, 0, 400)
    mel.connect_material_expressions(m2, "", final, "A")
    mel.connect_material_expressions(alpha, "", final, "B")
    mel.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("TIMEREVEAL_MAT sprite %s blend=%s" % (mat.get_path_name(), mat.get_editor_property("blend_mode")))


def import_noise():
    task = unreal.AssetImportTask()
    task.filename = str(NOISE_PNG)
    task.destination_path = "/Game/TimeReveal"
    task.destination_name = "T_TimeReveal_Noise"
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.load_asset("/Game/TimeReveal/T_TimeReveal_Noise")
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eal.save_loaded_asset(tex)
    return tex


def build_reveal_material(noise):
    src = "/Game/Stuff/Asset/Archelon/Archelon_Material"
    dst = "/Game/TimeReveal/M_ArchelonReveal"
    if eal.does_asset_exist(dst):
        eal.delete_asset(dst)
    if not eal.duplicate_asset(src, dst):
        raise RuntimeError("duplicate failed: " + src)
    mat = unreal.load_asset(dst)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", mat.get_editor_property("two_sided"))

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    dissolve = scalar(mat, "Dissolve", 1.0, -1400, 1400)
    tiling = scalar(mat, "NoiseTiling", 6.0, -1400, 1520)
    edge_w = scalar(mat, "EdgeWidth", 0.06, -1400, 1640)
    edge_s = scalar(mat, "EdgeStrength", 3.0, -1400, 1760)
    edge_c = vector(mat, "EdgeColor", srgb_to_linear("DDF5FF"), -1400, 1880)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -1400, 1200)
    uvs = node(unreal.MaterialExpressionMultiply, -1200, 1250)
    mel.connect_material_expressions(uv, "", uvs, "A")
    mel.connect_material_expressions(tiling, "", uvs, "B")
    ns = node(unreal.MaterialExpressionTextureSample, -1000, 1200)
    ns.set_editor_property("texture", noise)
    ns.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    mel.connect_material_expressions(uvs, "", ns, "UVs")

    diff = node(unreal.MaterialExpressionSubtract, -800, 1300)       # Dissolve - Noise
    mel.connect_material_expressions(dissolve, "", diff, "A")
    mel.connect_material_expressions(ns, "R", diff, "B")
    clip = node(unreal.MaterialExpressionConstant, -800, 1420)
    clip.set_editor_property("r", 0.3333)
    mask = node(unreal.MaterialExpressionAdd, -600, 1350)
    mel.connect_material_expressions(diff, "", mask, "A")
    mel.connect_material_expressions(clip, "", mask, "B")
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # Edge band: 1 - saturate(|Dissolve - Noise| / EdgeWidth)
    absn = node(unreal.MaterialExpressionAbs, -600, 1550)
    mel.connect_material_expressions(diff, "", absn, "")
    div = node(unreal.MaterialExpressionDivide, -450, 1550)
    mel.connect_material_expressions(absn, "", div, "A")
    mel.connect_material_expressions(edge_w, "", div, "B")
    satn = node(unreal.MaterialExpressionSaturate, -320, 1550)
    mel.connect_material_expressions(div, "", satn, "")
    band = node(unreal.MaterialExpressionOneMinus, -200, 1550)
    mel.connect_material_expressions(satn, "", band, "")
    # Only while dissolving (Dissolve < 1): gate = 1 - Dissolve  (so the finished model has no rim)
    gate = node(unreal.MaterialExpressionOneMinus, -200, 1680)
    mel.connect_material_expressions(dissolve, "", gate, "")
    gated = node(unreal.MaterialExpressionMultiply, -80, 1600)
    mel.connect_material_expressions(band, "", gated, "A")
    mel.connect_material_expressions(gate, "", gated, "B")
    e1 = node(unreal.MaterialExpressionMultiply, 40, 1600)
    mel.connect_material_expressions(gated, "", e1, "A")
    mel.connect_material_expressions(edge_s, "", e1, "B")
    e2 = node(unreal.MaterialExpressionMultiply, 160, 1650)
    mel.connect_material_expressions(e1, "", e2, "A")
    mel.connect_material_expressions(edge_c, "", e2, "B")
    mel.connect_material_property(e2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("TIMEREVEAL_MAT reveal %s blend=%s" % (dst, mat.get_editor_property("blend_mode")))


def ensure_label(folder, name):
    path = folder + "/" + name
    label = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if label is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
        label = tools.create_asset(name, folder, unreal.PrimaryAssetLabel, factory)
    label.set_editor_property("label_assets_in_my_directory", True)
    rules = label.get_editor_property("rules")
    rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    eal.save_loaded_asset(label)


build_sprite_material()
noise = import_noise()
build_reveal_material(noise)
ensure_label("/Game/TimeReveal", "TimeRevealCookLabel")
ensure_label("/Game/TimeReveal/FX", "TimeRevealFXCookLabel")
unreal.log("TIMEREVEAL_MAT_OK")
