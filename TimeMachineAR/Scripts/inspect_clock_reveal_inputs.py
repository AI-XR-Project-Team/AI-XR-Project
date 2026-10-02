"""Read-only audit for the clock-throw reveal design (UE 5.4 Python, -run=pythonscript).

Dumps CLOCK_AUDIT lines:
  - BP_TestGameMode pawn/controller classes
  - BP_DinoOverlay_* : parent, DinoInfo, mesh components, materials, graph nodes (StartReveal callers)
  - DA_DinoRegistry : marker -> species, fallback
  - DA_ARSession candidate images
  - Archelon meshes : triangles, material slots, blend modes, 'Alpha' parameter presence
  - NS_ActiveAtom / NS_Worm-Hole : emitters, sim target, renderers, user parameters, bounds
"""
import unreal

eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("CLOCK_AUDIT " + msg)


def safe(fn, default="?"):
    try:
        return fn()
    except Exception as exc:  # noqa: BLE001
        return "%s(%s)" % (default, str(exc)[:80])


# ---------------------------------------------------------------- game mode / pawn
gm = unreal.load_asset("/Game/Stuff/BP_TestGameMode")
if gm:
    gen = gm.generated_class()
    cdo = unreal.get_default_object(gen)
    log("gamemode pawn=%s pc=%s hud=%s" % (
        safe(lambda: cdo.get_editor_property("default_pawn_class")),
        safe(lambda: cdo.get_editor_property("player_controller_class")),
        safe(lambda: cdo.get_editor_property("hud_class"))))


# ---------------------------------------------------------------- overlay blueprints
def dump_blueprint(path):
    bp = unreal.load_asset(path)
    if bp is None:
        log("bp MISSING %s" % path)
        return
    gen = bp.generated_class()
    cdo = unreal.get_default_object(gen)
    log("bp %s parent=%s" % (path, safe(lambda: bp.get_editor_property("parent_class"))))
    log("   DinoInfo=%s Clickable=%s RevealDuration=%s AlphaParam=%s" % (
        safe(lambda: cdo.get_editor_property("dino_info")),
        safe(lambda: cdo.get_editor_property("clickable")),
        safe(lambda: cdo.get_editor_property("reveal_duration")),
        safe(lambda: cdo.get_editor_property("material_alpha_param_name"))))
    # Graph nodes: find function-call node titles.
    try:
        graphs = list(bp.get_editor_property("ubergraph_pages")) + list(bp.get_editor_property("function_graphs"))
    except Exception:  # noqa: BLE001
        graphs = []
    for g in graphs:
        try:
            nodes = g.get_editor_property("nodes")
        except Exception:  # noqa: BLE001
            continue
        for n in nodes:
            title = safe(lambda: n.get_node_title(unreal.NodeTitleType.FULL_TITLE))
            log("   node[%s] %s :: %s" % (safe(lambda: g.get_name()), n.get_class().get_name(), str(title).replace("\n", " | ")))
    # Component templates (SCS)
    try:
        scs = bp.get_editor_property("simple_construction_script")
        for node in scs.get_editor_property("root_nodes"):
            _dump_scs(node, 0)
    except Exception as exc:  # noqa: BLE001
        log("   scs unavailable (%s)" % str(exc)[:80])


def _dump_scs(node, depth):
    tmpl = safe(lambda: node.get_editor_property("component_template"))
    desc = "   " + "  " * depth + "scs %s" % safe(lambda: node.get_editor_property("internal_variable_name"))
    if isinstance(tmpl, unreal.StaticMeshComponent):
        mesh = tmpl.get_editor_property("static_mesh")
        desc += " mesh=%s rel=%s" % (mesh.get_path_name() if mesh else None, safe(lambda: tmpl.get_relative_transform()))
        for i in range(tmpl.get_num_materials()):
            desc += " mat%d=%s" % (i, safe(lambda: tmpl.get_material(i).get_path_name()))
    log(desc)
    for child in safe(lambda: node.get_editor_property("child_nodes"), []) or []:
        _dump_scs(child, depth + 1)


for name in ("BP_DinoOverlay", "BP_DinoOverlay_Archelon", "BP_DinoOverlay_T-Rex"):
    for folder in ("/Game/Stuff/", "/Game/Stuff/BluePrint/"):
        p = folder + name
        if eal.does_asset_exist(p):
            dump_blueprint(p)

# ---------------------------------------------------------------- registry / session
reg = unreal.load_asset("/Game/UI/DinoCard/DA_DinoRegistry")
if reg:
    log("registry fallback=%s" % safe(lambda: reg.get_editor_property("fallback_species")))
    for sp in safe(lambda: reg.get_editor_property("species"), []) or []:
        log("   species %s marker=%s exhibit_key=%s bone=%s flesh=%s xf=%s" % (
            sp.get_path_name(), safe(lambda: sp.get_editor_property("marker_code")),
            safe(lambda: sp.get_editor_property("exhibit_key")),
            safe(lambda: sp.get_editor_property("bone_mesh")), safe(lambda: sp.get_editor_property("flesh_mesh")),
            safe(lambda: sp.get_editor_property("mesh_transform"))))

sess = unreal.load_asset("/Game/Stuff/DA_ARSession") or unreal.load_asset("/Game/Stuff/Asset/DA_ARSession")
if sess:
    for c in sess.get_editor_property("candidate_images"):
        log("candidate %s %s w=%s" % (c.get_editor_property("friendly_name"),
                                     safe(lambda: c.get_editor_property("candidate_texture").get_path_name()),
                                     safe(lambda: c.get_editor_property("width"))))


# ---------------------------------------------------------------- archelon meshes
def dump_mesh(path):
    m = unreal.load_asset(path)
    if not isinstance(m, unreal.StaticMesh):
        log("mesh MISSING %s" % path)
        return
    log("mesh %s lods=%d tris(LOD0)=%s verts=%s bounds=%s" % (
        path, m.get_num_lods(), safe(lambda: m.get_num_triangles(0)), safe(lambda: m.get_num_vertices(0)),
        safe(lambda: m.get_bounding_box())))
    for i, sm in enumerate(m.get_editor_property("static_materials")):
        mat = sm.get_editor_property("material_interface")
        info = "   slot%d %s -> %s" % (i, sm.get_editor_property("material_slot_name"), mat.get_path_name() if mat else None)
        if mat:
            base = mat.get_base_material() if hasattr(mat, "get_base_material") else mat
            info += " blend=%s shading=%s" % (safe(lambda: base.get_editor_property("blend_mode")),
                                              safe(lambda: base.get_editor_property("shading_model")))
            try:
                info += " AlphaParam=%s" % unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(base, "Alpha")
            except Exception as exc:  # noqa: BLE001
                info += " AlphaParam?(%s)" % str(exc)[:40]
            try:
                names = unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat)
                info += " scalars=%s" % list(names)
            except Exception:  # noqa: BLE001
                pass
        log(info)


for p in ("/Game/Stuff/Asset/Archelon/Archelon", "/Game/Stuff/Asset/Archelon/Archelon_Bone",
          "/Game/Stuff/Asset/Turtle/Archelon"):
    if eal.does_asset_exist(p):
        dump_mesh(p)
for a in eal.list_assets("/Game/Stuff/Asset/Archelon", recursive=True):
    log("archelon asset %s" % a)


# ---------------------------------------------------------------- niagara
def dump_niagara(path):
    ns = unreal.load_asset(path)
    if ns is None:
        log("niagara MISSING %s" % path)
        return
    log("niagara %s fixedbounds=%s warmup=%s" % (path, safe(lambda: ns.get_editor_property("fixed_bounds")),
                                                  safe(lambda: ns.get_editor_property("warmup_time"))))
    try:
        for name in ("user_parameters", "exposed_parameters"):
            try:
                store = ns.get_editor_property(name)
                log("   %s = %s" % (name, str(store)[:400]))
            except Exception:  # noqa: BLE001
                pass
    except Exception:  # noqa: BLE001
        pass
    # Emitter handles
    try:
        handles = ns.get_editor_property("emitter_handles")
    except Exception as exc:  # noqa: BLE001
        log("   emitter_handles unavailable (%s)" % str(exc)[:80])
        handles = []
    for h in handles:
        em = safe(lambda: h.get_editor_property("instance"))
        info = "   emitter %s enabled=%s" % (safe(lambda: h.get_editor_property("name")), safe(lambda: h.get_editor_property("is_enabled")))
        if isinstance(em, unreal.NiagaraEmitter):
            info += " simtarget=%s localspace=%s bounds=%s" % (
                safe(lambda: em.get_editor_property("sim_target")), safe(lambda: em.get_editor_property("local_space")),
                safe(lambda: em.get_editor_property("fixed_bounds")))
            try:
                for r in em.get_editor_property("renderer_properties"):
                    rinfo = " renderer=%s" % r.get_class().get_name()
                    for prop in ("material", "particle_mesh", "sub_image_size"):
                        try:
                            rinfo += " %s=%s" % (prop, r.get_editor_property(prop))
                        except Exception:  # noqa: BLE001
                            pass
                    info += rinfo
            except Exception as exc:  # noqa: BLE001
                info += " renderers?(%s)" % str(exc)[:60]
        else:
            info += " instance=%s" % str(em)[:80]
        log(info)


for p in ("/Game/FreeNiagaraPack/Effects/NS_ActiveAtom", "/Game/FreeNiagaraPack/Effects/NS_Worm-Hole"):
    dump_niagara(p)
log("pack assets: %d" % len(eal.list_assets("/Game/FreeNiagaraPack", recursive=True)))
log("DONE")
