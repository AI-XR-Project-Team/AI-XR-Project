"""Read-only audit of AR candidate images used by the packaged session."""
import unreal
session = unreal.load_asset('/Game/Stuff/Asset/DA_ARSession')
for candidate in session.get_editor_property('candidate_images'):
    unreal.log('NAV_MARKER_AUDIT: %s | %s | %s' % (
        candidate.get_path_name(), candidate.get_editor_property('friendly_name'),
        candidate.get_editor_property('candidate_texture').get_path_name()))
