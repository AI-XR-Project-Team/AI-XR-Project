# Navigation skin

Source PNGs: `docs/ai-handoff/inbox/sheet`. Imported assets: `/Game/UI/Nav/Skin`.

Run `TimeMachineAR/Scripts/import_nav_skin.py` with Unreal Editor Python to reimport.
It creates UI textures and an AlwaysCook PrimaryAssetLabel for path-loaded assets.

The full-map class keeps the existing bound MapView and destination delegates, and
reparents it into a 940 × 1672 layout scaled to fit. Destination labels still come
from the graph. Follow minimap, routing, localization and AR guidance remain on
their existing code paths. The legacy guidance bar hides while the full map's
Lexi bubble is visible.

The supplied floorplan does not align to the server graph, so the skin draws the
actual graph polygon with a tiled floor and places stair art in existing obstacle
bounds. The supplied ankylosaurus POI depicts the wrong dinosaur; that destination
retains the existing correct icon. The museum photograph in the reference is not
among the supplied assets; the background is a dark overlay.

Validation: build `TimeMachineAREditor Win64 Development`, then run the automation
test `TimeMachineAR.Nav.SkinPreview` with rendering enabled (`-RenderOffscreen`,
not `-nullrhi`). It uses the repository's neuti4f graph and writes
`TimeMachineAR/Saved/NavSkinPreview.png`. This is an editor render, not an Android
device/end-to-end navigation test.
