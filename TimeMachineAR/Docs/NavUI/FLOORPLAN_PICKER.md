# Museum floor-plan picker — 2026-09-16

## Final interaction
- T_MuseumFloorplan preserves the supplied navy/gold visual style with the entrance relocated and amethyst restored at the user's requested lower-left position.
- UMG uses a normalized 429 x 555 illustration coordinate space for the higher-resolution texture, cropping its incomplete footer at source y=532.
- Retains the approved Lexi illustration, a speech bubble, camera blur and top-left Back.
- Bottom gallery tabs: All, Cenozoic, Paleozoic (central and lower-right rooms), Marine, Minerals.
- Gallery selection previews a subtle image-space polygon overlay and clears any prior specimen selection.
- A specimen tap previews a ring and a named bottom card. It does not request a route or close the map.
- Only the enabled Here to guide CTA broadcasts the live destination node ID and closes the picker.
- Missing/removed live nodes disable confirmation. Pose/graph updates preserve valid pending selections.
- Confirmation latches before broadcasting so repeated or reentrant submissions cannot start another route.

## Coordinate contract
Hotspots and gallery boundaries are manually aligned UI geometry in source-image pixels. They are not surveyed metric boundaries and must not be used to modify routing, walls, distance or AR localization. Existing museum_final navigation coordinates and node IDs remain unchanged. Gallery names follow the previously approved museum categories.
The illustration prints Entrance as 1 at (50,308), resolving the existing entrance label (internal display key 2). Amethyst prints as 2 at (50,486), resolving the existing amethyst label (internal key 1). All 13 destinations are visible and selectable. These two illustrated positions follow the user's earlier reference; backend metric coordinates remain unchanged. The printed 5 m decoration is not used as a scale.

## Files
- Source/TimeMachineAR/NavFullMapWidget.cpp and .h: layout, gallery preview, selection and confirmation.
- Source/TimeMachineAR/Tests/NavSkinTest.cpp: new image hit regions, data refresh behavior, explicit confirmation, multiple aspect ratios and gallery renders.
- Asset/NavReference/T_MuseumFloorplan.png: revised source (PNG files are generally ignored by this repository).
- Content/UI/Nav/Reference/T_MuseumFloorplan.uasset: imported UI texture, non-streaming, no mipmaps.
- Scripts/import_museum_floorplan.py: repeatable Unreal texture import.

## Verification
- Win64 editor build succeeds using ModuleWithSuffix because the user's existing editor holds the normal DLL open. The original editor is not closed or its unsaved state discarded.
- TimeMachineAR.Nav.SkinPreview and TimeMachineAR.Nav.Destinations automation tests.
- Real UMG offscreen renders: Saved/NavSkinPreview.png, _Selected, _Paleo, _Ceno, _Mineral, _Tall (1080 x 2340), _Compact (540 x 960).
- Android Development ASTC package: Saved/MuseumFloorplanBuild/Android_ASTC/TimeMachineAR-arm64.apk.
- Phone touch verification requires unlocking the connected R3CY8009NKF. Physical museum walking/AR alignment is not verified by UI tests.

## Follow-up refinement
- Selected gallery fill increased to 10% cyan with a clearer outline; adjacent filled spans avoid Android DPI scanline gaps.
- Lexi now introduces the chosen gallery/specimen with two short lines, including entrance/exit-specific wording.
- Tapping the same specimen again clears both selection and gallery emphasis; tapping the same gallery again returns to All.
- Deselect restores Lexi's invitation and disables the guidance CTA.
- Regression checks cover specimen toggle, gallery toggle, no premature navigation commit, and committing the correct label on confirmation.
- Live museum_final API read-only check passed: 38 nodes, 38 edges, 13 destinations, 12 entrance-to-destination routes.

## Device result
Final APK installed successfully with adb install -r on R3CY8009NKF; existing app data retained. On-device screenshots verified the stronger continuous gallery fill (without the earlier line gaps), Allosaurus-specific two-line Lexi text, enabled guidance CTA after selection, and a second tap returning to All with the default Lexi invitation and disabled CTA. Device screenshots: Saved/floorplan_phone_final_launch.png and Saved/floorplan_phone_final_deselected.png. UI confirmation and metric routing are covered independently by automation and live API checks; no claim of a physical museum walkthrough is made.

## Entrance/amethyst artwork revision
- Built-in imagegen edit; selected output copied into Asset/NavReference/T_MuseumFloorplan.png and imported with Scripts/import_museum_floorplan.py.
- Prompt: keep navy/gray-blue map and existing exhibits; move cyan entrance 1 below exit, restore amethyst 2 at the lower left, and give its crystal and ring the same yellow/gold glow as other specimens. Final refinement recolors only the amethyst crystal yellow/gold.
- Mineral highlight follows the revised lower-left illustration, including amethyst. The edited illustration is presentation artwork, not a replacement for museum_final wall data.
- Regression checks resolve both positions by live labels despite their swapped printed numbers, include all 13 destinations, and check amethyst selection/toggle. Dedicated Entrance and Amethyst UMG screenshots are saved by SkinPreview.
- Revision validation: editor suffix 9178 built successfully; SkinPreview and Destinations both passed (Saved/Logs/MuseumEntranceTests.log); Android packaging succeeded (Saved/MuseumEntranceBuild.log); adb install -r returned Success on R3CY8009NKF. The installed map is visible in Saved/entrance_revision_phone_map.png. The user resumed using the phone during touch checks, so the two relocated destinations' selection/toggle assertions are verified by automation, not claimed as an uninterrupted physical touch test.
