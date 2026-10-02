"""UE5.4: create the opt-in Archelon entrance profile; preserve all existing UI/data fields."""
import unreal
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
def required(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError('Missing required entrance asset: ' + path)
    return asset
path = '/Game/TimeReveal/DA_TimeReveal_Archelon'
profile = unreal.load_asset(path) if assets.does_asset_exist(path) else None
if profile is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.TimeRevealProfile)
    profile = tools.create_asset('DA_TimeReveal_Archelon', '/Game/TimeReveal', unreal.TimeRevealProfile, factory)
profile.set_editor_property('watch_class', unreal.TimeWatchActor.static_class())
profile.set_editor_property('orbit_fx', required('/Game/TimeReveal/FX/NS_WatchOrbit'))
profile.set_editor_property('portal_fx', required('/Game/TimeReveal/FX/NS_TimePortal'))
profile.set_editor_property('sprite_material', required('/Game/TimeReveal/FX/M_TimeFX_Sprite'))
profile.set_editor_property('reveal_material', required('/Game/TimeReveal/M_ArchelonReveal'))
profile.set_editor_property('primary_color', unreal.LinearColor(0.07036,0.36625,1.0,1.0))
profile.set_editor_property('secondary_color', unreal.LinearColor(0.69387,0.40198,0.09531,1.0))
profile.set_editor_property('enabled', True)
profile.set_editor_property('ready_screen_anchor', unreal.Vector2D(0.5, 0.60))
assets.save_loaded_asset(profile)
dino = required('/Game/UI/DinoCard/DA_Dino_Archelon')
if dino.get_editor_property('marker_code') != 'EX5-ARCHELON':
    raise RuntimeError('Unexpected Archelon marker mapping; inspect before assigning profile')
dino.set_editor_property('time_reveal_profile', profile)
assets.save_loaded_asset(dino)
unreal.log('TIME_REVEAL_PROFILE_OK ' + profile.get_path_name())
