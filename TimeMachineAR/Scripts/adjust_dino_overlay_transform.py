#!/usr/bin/env python3
"""BP_DinoOverlay_T-Rex 의 BoneMesh / FleshMesh 상대 트랜스폼을 조정한다.

AR 앱으로 촬영했을 때 3D 모델이 실제 화석보다 크고 위치가 어긋나는 문제를
수정하기 위한 UE 에디터 파이썬 스크립트이다.

조절 값(SCALE, LOCATION_OFFSET, ROTATION_OFFSET)을 바꿔 가며 여러 번
돌려도 안전하다(매번 절대값으로 덮어쓴다).

실행(에디터 내 Python 콘솔 또는 헤드리스):
  # 에디터 내:  File → Execute Python Script → 이 파일 선택
  # 또는 Output Log 의 Cmd 에서:
  #   py "C:/.../TimeMachineAR/Scripts/adjust_dino_overlay_transform.py"
  #
  # 헤드리스(리포 루트에서):
  #   "<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ^
  #     "<repo>/TimeMachineAR/TimeMachineAR.uproject" ^
  #     -run=pythonscript ^
  #     -script="<repo>/TimeMachineAR/Scripts/adjust_dino_overlay_transform.py" ^
  #     -unattended -nosplash
"""
import unreal

# ============================================================================
#  조절 값  —  여기만 바꾸면 된다
# ============================================================================

# 스케일 (X, Y, Z).  현재 모델이 실제 화석보다 크므로 줄인다.
# 1.0 이 원본 크기. 사진 기준으로 약 70% 로 축소하면 맞을 것 같다.
# → 현장에서 미세 조정하려면 이 값을 0.01 단위로 올리거나 내린다.
SCALE = unreal.Vector(0.7, 0.7, 0.7)

# 위치 오프셋 (cm 단위, 언리얼 기본). X=전/후, Y=좌/우, Z=상/하.
# 모델이 약간 위로 떠 있으므로 Z 를 내린다.
LOCATION_OFFSET = unreal.Vector(0.0, 0.0, -15.0)

# 회전 오프셋 (도 단위). 필요하면 여기서 미세 회전.
ROTATION_OFFSET = unreal.Rotator(0.0, 0.0, 0.0)

# ============================================================================
#  대상 블루프린트 경로
# ============================================================================
BP_PATH = "/Game/Stuff/BluePrint/BP_DinoOverlay_T-Rex.BP_DinoOverlay_T-Rex"

# 수정할 컴포넌트 이름들 (ADinoOverlayActor 생성자에서 만드는 이름과 동일)
TARGET_COMPONENTS = ["BoneMesh", "FleshMesh"]


# ============================================================================
#  본체
# ============================================================================
def run():
    lib = unreal.EditorAssetLibrary

    if not lib.does_asset_exist(BP_PATH):
        unreal.log_error(f"[adjust] 에셋 없음: {BP_PATH}")
        return False

    bp_asset = lib.load_asset(BP_PATH)
    if bp_asset is None:
        unreal.log_error(f"[adjust] 로드 실패: {BP_PATH}")
        return False

    # Blueprint 에서 SimpleConstructionScript → 노드 트리를 통해 컴포넌트 접근
    bp_gen_class = bp_asset.generated_class()
    if bp_gen_class is None:
        unreal.log_error("[adjust] GeneratedClass 가 없습니다")
        return False

    cdo = unreal.get_default_object(bp_gen_class)
    if cdo is None:
        unreal.log_error("[adjust] CDO(Class Default Object) 를 얻지 못했습니다")
        return False

    # CDO 에서 컴포넌트를 이름으로 찾는다
    modified = False
    for comp_name in TARGET_COMPONENTS:
        # CDO 의 프로퍼티로 직접 접근
        comp = cdo.get_editor_property(comp_name)
        if comp is None:
            unreal.log_warning(f"[adjust] 컴포넌트 '{comp_name}' 을(를) CDO에서 찾지 못했습니다")
            continue

        # 새 트랜스폼 구성
        new_transform = unreal.Transform(
            location=LOCATION_OFFSET,
            rotation=ROTATION_OFFSET,
            scale=SCALE,
        )

        old_transform = comp.get_editor_property("relative_transform")
        unreal.log(
            f"[adjust] {comp_name}: 기존 트랜스폼\n"
            f"   위치={old_transform.translation}  "
            f"회전={old_transform.rotation.rotator()}  "
            f"스케일={old_transform.scale3d}"
        )

        comp.set_editor_property("relative_transform", new_transform)
        unreal.log(
            f"[adjust] {comp_name}: 새 트랜스폼\n"
            f"   위치={LOCATION_OFFSET}  "
            f"회전={ROTATION_OFFSET}  "
            f"스케일={SCALE}"
        )
        modified = True

    if modified:
        # 에셋 저장
        lib.save_asset(BP_PATH)
        unreal.log(f"[adjust] ✅ 저장 완료: {BP_PATH}")
    else:
        unreal.log_warning("[adjust] 변경된 컴포넌트가 없습니다")

    return modified


# ============================================================================
#  SCS(SimpleConstructionScript) 방식 — CDO 직접 접근이 안 될 때의 대안
# ============================================================================
def run_via_scs():
    """Blueprint 의 SCS 노드 트리를 탐색해 컴포넌트 템플릿을 직접 수정한다.

    CDO 방식이 동작하지 않을 때(UE 버전 차이 등) 이 함수를 쓴다.
    """
    lib = unreal.EditorAssetLibrary

    bp_asset = lib.load_asset(BP_PATH)
    if bp_asset is None:
        unreal.log_error(f"[adjust/scs] 로드 실패: {BP_PATH}")
        return False

    scs = bp_asset.get_editor_property("simple_construction_script")
    if scs is None:
        unreal.log_error("[adjust/scs] SCS 없음")
        return False

    all_nodes = scs.get_all_nodes()
    modified = False

    for node in all_nodes:
        template = node.get_editor_property("component_template")
        if template is None:
            continue

        name = template.get_name()
        if name not in TARGET_COMPONENTS:
            continue

        # StaticMeshComponent 의 RelativeTransform 수정
        new_transform = unreal.Transform(
            location=LOCATION_OFFSET,
            rotation=ROTATION_OFFSET,
            scale=SCALE,
        )

        old_loc = template.get_editor_property("relative_location")
        old_rot = template.get_editor_property("relative_rotation")
        old_scale = template.get_editor_property("relative_scale3d")
        unreal.log(
            f"[adjust/scs] {name}: 기존 → 위치={old_loc}  회전={old_rot}  스케일={old_scale}"
        )

        template.set_editor_property("relative_location", LOCATION_OFFSET)
        template.set_editor_property("relative_rotation", ROTATION_OFFSET)
        template.set_editor_property("relative_scale3d", SCALE)
        unreal.log(
            f"[adjust/scs] {name}: 변경 → 위치={LOCATION_OFFSET}  회전={ROTATION_OFFSET}  스케일={SCALE}"
        )
        modified = True

    if modified:
        lib.save_asset(BP_PATH)
        unreal.log(f"[adjust/scs] ✅ 저장 완료: {BP_PATH}")
    else:
        unreal.log_warning("[adjust/scs] 대상 컴포넌트를 찾지 못했습니다")

    return modified


# ============================================================================
#  실행
# ============================================================================
unreal.log("=" * 60)
unreal.log("[adjust] 공룡 오버레이 트랜스폼 조정 시작")
unreal.log("=" * 60)

# 먼저 CDO 방식 시도, 실패하면 SCS 방식으로 폴백
try:
    ok = run()
except Exception as e:
    unreal.log_warning(f"[adjust] CDO 방식 실패 ({e}), SCS 방식으로 전환")
    ok = False

if not ok:
    try:
        ok = run_via_scs()
    except Exception as e:
        unreal.log_error(f"[adjust] SCS 방식도 실패: {e}")

if ok:
    unreal.log("[adjust] 🎉 완료! 에디터에서 BP_DinoOverlay_T-Rex 를 열어 확인하세요.")
else:
    unreal.log_error(
        "[adjust] 자동 수정 실패. 에디터에서 BP_DinoOverlay_T-Rex 를 열고\n"
        "  BoneMesh / FleshMesh 컴포넌트의 Transform 을 수동으로 조정하세요.\n"
        f"  권장 스케일: {SCALE}\n"
        f"  권장 위치:   {LOCATION_OFFSET}\n"
        f"  권장 회전:   {ROTATION_OFFSET}"
    )
