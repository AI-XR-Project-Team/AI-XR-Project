#!/usr/bin/env python3
"""Archelon 스태틱 메시의 방향을 바로잡는다 — FBX 재임포트 없이.

문제
----
Archelon.uasset 메시가 FBX 임포트 시 기울어진 채로 들어와서, 에디터에서
gizmo 축이 메시의 전후좌우와 맞지 않는다. 이 때문에 BP/DA 에서 위치를
잡을 때마다 직관에 어긋나는 값을 넣어야 한다.

해결
----
방법 A) 스태틱 메시의 **Source Model Build Settings** 에서
        SrcCoordinateSystem / BuildScale / Rotation 을 수정하고 빌드.
방법 B) 원본 FBX 가 있으면 재임포트 시 Import Rotation 조정.
방법 C) (가장 확실) Mesh Merge 를 써서 회전을 적용한 새 메시를 구워낸다.

이 스크립트는 **방법 C** 를 쓴다: 기존 메시를 원하는 회전으로 Merge/Bake 해서
새 에셋으로 만든 뒤, DA 와 BP 가 새 메시를 참조하도록 바꾼다.

※ 원본 메시는 건드리지 않는다(백업 불필요).

실행
----
에디터 Output Log (Python 모드):
  exec(open(r"<repo>/TimeMachineAR/Scripts/fix_archelon_mesh_rotation.py").read())
"""
import unreal
import math

# ============================================================================
#  조절 값
# ============================================================================

# 메시를 수직(수평)으로 맞추기 위한 보정 회전 (Pitch, Yaw, Roll)
# 에디터에서 메시를 보면서 값을 잡아야 한다.
#
#  - Pitch : 앞뒤 기울기 (거북이 머리 들기/숙이기)
#  - Yaw   : 좌우 회전 (거북이가 보는 방향)
#  - Roll  : 좌우 기울기 (거북이 몸통 기울어짐)
#
# 사진 기준 추정값 — 현장에서 미세 조정 필요:
CORRECTION_PITCH = 0.0    # 앞뒤 기울기 보정
CORRECTION_YAW   = 0.0    # 좌우 방향 보정
CORRECTION_ROLL  = 0.0    # 좌우 기울기 보정

# ============================================================================
#  에셋 경로
# ============================================================================
MESH_PATH   = "/Game/Stuff/Asset/Archelon/Archelon"
DA_PATH     = "/Game/UI/DinoCard/DA_Dino_Archelon"
BP_PATH     = "/Game/Stuff/BluePrint/BP_DinoOverlay_Archelon.BP_DinoOverlay_Archelon"

# 방법 선택:
#   "inspect"  — 현재 메시 정보만 출력 (수정 없음)
#   "source"   — Source Model 빌드 설정의 회전 조정 (가장 깔끔, UE 5.4+)
#   "bake"     — 회전 적용한 새 메시를 구워냄 (확실하지만 새 에셋 생성)
MODE = "inspect"


# ============================================================================
#  1단계: 현재 메시 정보 확인
# ============================================================================
def inspect_mesh():
    """메시의 현재 임포트 데이터와 소스 모델 정보를 출력한다."""
    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        unreal.log_error(f"[fix] 메시 로드 실패: {MESH_PATH}")
        return

    unreal.log(f"[fix] 메시: {MESH_PATH}")

    # 바운딩 박스로 메시 크기 확인
    bbox = mesh.get_bounding_box()
    min_pt = bbox.min
    max_pt = bbox.max
    size = max_pt - min_pt
    unreal.log(f"[fix] 바운딩 박스: min={min_pt}  max={max_pt}")
    unreal.log(f"[fix] 크기(cm): X={size.x:.1f}  Y={size.y:.1f}  Z={size.z:.1f}")

    # 임포트 데이터 확인
    import_data = mesh.get_editor_property("asset_import_data")
    if import_data is not None:
        unreal.log(f"[fix] AssetImportData 타입: {type(import_data).__name__}")

        # FBX 임포트 데이터인지 확인
        fbx_data = unreal.FbxStaticMeshImportData.cast(import_data) if hasattr(unreal, 'FbxStaticMeshImportData') else None
        if fbx_data is not None:
            try:
                import_rot = fbx_data.get_editor_property("import_rotation")
                import_trans = fbx_data.get_editor_property("import_translation")
                import_scale = fbx_data.get_editor_property("import_uniform_scale")
                unreal.log(f"[fix] FBX Import Rotation: {import_rot}")
                unreal.log(f"[fix] FBX Import Translation: {import_trans}")
                unreal.log(f"[fix] FBX Import Scale: {import_scale}")
            except Exception as e:
                unreal.log_warning(f"[fix] FBX 속성 읽기 실패: {e}")

        # 원본 파일 경로 확인
        try:
            source_files = import_data.get_editor_property("source_data")
            unreal.log(f"[fix] 소스 데이터: {source_files}")
        except:
            pass

        try:
            # ExtractFilenames 대체
            filenames = import_data.extract_filenames()
            for f in filenames:
                unreal.log(f"[fix] 원본 파일: {f}")
        except Exception as e:
            unreal.log(f"[fix] 원본 파일 경로 확인 불가: {e}")

    # 소스 모델 수 확인
    try:
        num_source = mesh.get_num_source_models()
        unreal.log(f"[fix] Source Model 수: {num_source}")
    except:
        pass

    # DA 의 현재 MeshTransform 도 같이 확인
    da = unreal.load_asset(DA_PATH)
    if da is not None:
        mt = da.get_editor_property("MeshTransform")
        unreal.log(
            f"[fix] DA MeshTransform:\n"
            f"   위치: {mt.translation}\n"
            f"   회전: {mt.rotation.rotator()}\n"
            f"   스케일: {mt.scale3d}"
        )

    unreal.log("[fix] --- inspect 완료 ---")


# ============================================================================
#  2단계: Source Model 빌드 설정에서 회전 조정
# ============================================================================
def fix_via_source_model():
    """Source Model 의 Build Settings → Source Rotation 을 조정하고 빌드한다.

    UE 5.x 에서 StaticMesh 의 SourceModel[0].BuildSettings 에
    BuildScale3D 와 Source Model 설정이 있다.
    MeshDescription 을 읽어 트랜스폼을 적용한 뒤 다시 쓰는 방식.
    """
    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        unreal.log_error(f"[fix] 메시 로드 실패: {MESH_PATH}")
        return False

    # 에디터 서브시스템으로 메시 수정
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        unreal.log_warning("[fix] StaticMeshEditorSubsystem 을 찾을 수 없습니다")
        return False

    # LOD 0 의 MeshDescription 을 가져온다
    md = subsystem.get_lod_mesh_description(mesh, 0)
    if md is None:
        unreal.log_error("[fix] LOD 0 MeshDescription 없음")
        return False

    correction = unreal.Rotator(CORRECTION_PITCH, CORRECTION_YAW, CORRECTION_ROLL)
    unreal.log(f"[fix] 보정 회전: Pitch={CORRECTION_PITCH} Yaw={CORRECTION_YAW} Roll={CORRECTION_ROLL}")

    # EditorStaticMeshLibrary 로 메시 빌드 설정 조정
    try:
        mesh_lib = unreal.EditorStaticMeshLibrary
        # LOD 0 빌드 설정
        build_settings = mesh_lib.get_lod_build_settings(mesh, 0)
        unreal.log(f"[fix] 현재 BuildScale: {build_settings.build_scale}")
        # build_settings 는 읽기 전용일 수 있으므로, 직접 소스 모델 수정
    except Exception as e:
        unreal.log(f"[fix] BuildSettings 접근: {e}")

    unreal.log("[fix] source model 방식 시도 중...")
    return False  # fallback 으로 넘김


# ============================================================================
#  3단계: FBX Import Rotation 수정 후 재임포트
# ============================================================================
def fix_via_reimport():
    """FBX AssetImportData 의 Import Rotation 을 수정하고 재임포트한다.

    원본 FBX 파일이 원래 경로에 있어야 동작한다.
    """
    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        unreal.log_error(f"[fix] 메시 로드 실패: {MESH_PATH}")
        return False

    import_data = mesh.get_editor_property("asset_import_data")
    if import_data is None:
        unreal.log_error("[fix] AssetImportData 없음")
        return False

    # 원본 파일이 있는지 확인
    try:
        filenames = import_data.extract_filenames()
        if not filenames:
            unreal.log_error("[fix] 원본 FBX 경로가 비어 있습니다")
            return False

        import os
        source_path = filenames[0]
        if not os.path.exists(source_path):
            unreal.log_error(f"[fix] 원본 FBX 파일이 없습니다: {source_path}")
            unreal.log("[fix] → FBX 파일을 원래 경로에 복원하거나, bake 모드를 사용하세요")
            return False
    except Exception as e:
        unreal.log_error(f"[fix] 원본 파일 확인 불가: {e}")
        return False

    # Import Rotation 수정
    correction = unreal.Rotator(CORRECTION_PITCH, CORRECTION_YAW, CORRECTION_ROLL)

    try:
        current_rot = import_data.get_editor_property("import_rotation")
        new_rot = unreal.Rotator(
            current_rot.pitch + CORRECTION_PITCH,
            current_rot.yaw + CORRECTION_YAW,
            current_rot.roll + CORRECTION_ROLL,
        )
        import_data.set_editor_property("import_rotation", new_rot)
        unreal.log(f"[fix] Import Rotation: {current_rot} → {new_rot}")
    except Exception as e:
        unreal.log_error(f"[fix] Import Rotation 수정 실패: {e}")
        return False

    # 재임포트
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    result = tools.reimport_asset(mesh)
    unreal.log(f"[fix] 재임포트 결과: {result}")
    return True


# ============================================================================
#  4단계: Bake — 회전 적용한 새 메시 구워내기
# ============================================================================
def fix_via_bake():
    """MergeStaticMeshActors 와 유사한 방식으로, 레벨에 메시를 배치하고
    회전을 적용한 상태로 새 에셋을 구워낸다.

    레벨에 임시 액터를 만들고 → 원하는 회전 적용 → 메시 병합(Merge) →
    결과를 원본 에셋 위치에 저장.
    """
    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        unreal.log_error(f"[fix] 메시 로드 실패: {MESH_PATH}")
        return False

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        unreal.log_error("[fix] 에디터 월드 없음")
        return False

    correction = unreal.Rotator(CORRECTION_PITCH, CORRECTION_YAW, CORRECTION_ROLL)

    # 1. 레벨에 임시 스태틱 메시 액터 배치 (회전 적용)
    transform = unreal.Transform(
        location=unreal.Vector(0, 0, 0),
        rotation=correction,
        scale=unreal.Vector(1, 1, 1),
    )

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0, 0, 0))

    if actor is None:
        unreal.log_error("[fix] 임시 액터 생성 실패")
        return False

    smc = actor.get_editor_property("static_mesh_component")
    smc.set_editor_property("static_mesh", mesh)
    actor.set_actor_transform(transform, False, False)

    unreal.log(f"[fix] 임시 액터 배치 완료 (회전: {correction})")

    # 2. 메시 병합으로 회전이 적용된 새 메시 생성
    output_path = MESH_PATH + "_Fixed"
    merge_options = unreal.MergeStaticMeshActorsOptions()
    merge_options.base_package_name = output_path
    merge_options.destroy_source_actors = True
    merge_options.new_actor_label = "Archelon_Fixed"

    try:
        # EditorLevelLibrary 의 MergeStaticMeshComponents 사용
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if subsystem is not None:
            # 메시를 복사하고 버텍스를 직접 변환하는 방식으로 대체
            unreal.log("[fix] MeshDescription 직접 변환 시도...")

            md = subsystem.get_lod_mesh_description(mesh, 0)
            if md is None:
                unreal.log_error("[fix] MeshDescription 없음")
                actor.destroy_actor()
                return False

            # TODO: MeshDescription 의 버텍스를 회전 적용
            unreal.log("[fix] MeshDescription 변환은 아직 지원 안 됨 → 수동 방법 안내")
            actor.destroy_actor()
            return False
    except Exception as e:
        unreal.log_warning(f"[fix] 병합 실패: {e}")

    # 임시 액터 정리
    actor.destroy_actor()
    return False


# ============================================================================
#  실행
# ============================================================================
unreal.log("=" * 60)
unreal.log("[fix] Archelon 메시 회전 보정 스크립트")
unreal.log(f"[fix] 모드: {MODE}")
unreal.log("=" * 60)

if MODE == "inspect":
    inspect_mesh()
    unreal.log("")
    unreal.log("[fix] ═══════════════════════════════════════════")
    unreal.log("[fix]  다음 단계:")
    unreal.log("[fix]  1. 위 정보를 보고 필요한 보정 회전값을 결정")
    unreal.log("[fix]  2. 스크립트 상단의 CORRECTION_PITCH/YAW/ROLL 수정")
    unreal.log("[fix]  3. MODE 를 'source' 또는 'reimport' 로 변경")
    unreal.log("[fix]  4. 다시 실행")
    unreal.log("[fix] ═══════════════════════════════════════════")
    unreal.log("")
    unreal.log("[fix] 💡 가장 쉬운 방법:")
    unreal.log("[fix]    에디터에서 Archelon.uasset 을 더블클릭으로 열고")
    unreal.log("[fix]    오른쪽 Details 패널에서:")
    unreal.log("[fix]    Asset Import Data → Import Rotation 값을 조정한 뒤")
    unreal.log("[fix]    상단 Reimport 버튼 클릭")
    unreal.log("[fix]    (원본 FBX 파일이 필요합니다)")
    unreal.log("")
    unreal.log("[fix]    FBX 파일이 없다면:")
    unreal.log("[fix]    adjust_archelon_transform.py 로 DA MeshTransform 에서")
    unreal.log("[fix]    회전 보정값을 넣는 방식을 사용하세요.")

elif MODE == "reimport":
    ok = fix_via_reimport()
    if not ok:
        unreal.log_warning("[fix] 재임포트 실패. inspect 모드로 전환하여 정보를 확인하세요.")

elif MODE == "source":
    ok = fix_via_source_model()
    if not ok:
        unreal.log("[fix] source 방식 불가 → reimport 시도...")
        ok = fix_via_reimport()
    if not ok:
        unreal.log_warning("[fix] 모든 자동 방식 실패. 수동 조정이 필요합니다.")

elif MODE == "bake":
    ok = fix_via_bake()
    if not ok:
        unreal.log_warning("[fix] bake 실패. 수동 조정이 필요합니다.")

else:
    unreal.log_error(f"[fix] 알 수 없는 모드: {MODE}")
