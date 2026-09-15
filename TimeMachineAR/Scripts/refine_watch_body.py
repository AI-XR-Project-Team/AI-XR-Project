"""Retain dial numerals and the bow silhouette; source geometry remains unchanged."""
import unreal
unreal.load_module('StaticMeshEditor')
mesh=unreal.load_asset('/Game/TimeReveal/Clock/SM_Watch_Body')
settings=unreal.StaticMeshReductionSettings(percent_triangles=0.05,screen_size=1.0)
options=unreal.StaticMeshReductionOptions(reduction_settings=[settings])
if unreal.EditorStaticMeshLibrary.set_lods(mesh,options)<0: raise RuntimeError('Reduction failed')
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.log('WATCH_BODY_QUALITY tris='+str(mesh.get_num_triangles(0)))
