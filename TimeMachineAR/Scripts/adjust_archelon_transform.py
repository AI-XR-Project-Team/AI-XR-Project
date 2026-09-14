#!/usr/bin/env python3
"""DA_Dino_Archelon 의 MeshTransform 을 설정한다 — 축 보정 + 정면 회전.

문제
----
Archelon 메시가 FBX 임포트 시 회전된 상태로 들어와, BP_DinoOverlay_Archelon
에서 gizmo 축(X/Y/Z 방향선)이 틀어져 있다. 이 때문에:
  ① BP 에서 위치를 잡아 복붙하면 DA 의 MeshTransform.Rotation 과 맞물려
     이동 방향이 어긋난다.
  ② 거북이가 사용자를 정면으로 바라보게 회전을 잡기가 어렵다.

해결
----
SetRelativeTransform(FTransform) 은 내부적으로 회전·위치·스케일을 한 번에
적용한다. 여기서 Location 은 **부모(Root) 좌표계** 기준이다. 즉:
  - Rotation 을 아무리 돌려도 Location 은 부모 축 기준이다.
  - 그러므로 BP 에서 gizmo 가 틀어져 보여도, DA MeshTransform 의 Location 은
    부모(=액터 Root=AR 마커 방향) 기준 X/Y/Z 그대로 동작한다.

이 스크립트는:
  1. 원하는 **회전값**(거북이 정면 바라보기)을 설정
  2. **위치 오프셋**을 부모 좌표계 기준으로 설정
  3. **스케일**을 설정
  4. DA_Dino_Archelon 의 MeshTransform 에 저장

사용법
------
상단의 ROTATION, LOCATION, SCALE 값만 조정한 후 에디터에서 실행.
BP 에서 맞춘 값을 복붙할 필요 없이, 이 스크립트가 직접 DA 에 기록한다.

실행:
  에디터 Output Log (Python 모드) 에서:
    exec(open(r"<repo>/TimeMachineAR/Scripts/adjust_archelon_transform.py").read())
"""
import unreal
import math

# ============================================================================
#  조절 값  —  여기만 바꾸면 된다
# ============================================================================

# --- 회전 (Pitch, Yaw, Roll) 단위: 도(°) ---
# AR 마커가 바닥에 놓이면 기본적으로:
#   X = 마커의 "오른쪽"
#   Y = 마커의 "앞쪽"  
#   Z = 마커의 "위쪽"
#
# 거북이가 사용자를 정면으로 바라보게 하려면:
#   - 사용자가 마커 앞에 서 있다고 가정
#   - 거북이 머리가 -Y 방향(마커 뒤→앞, 즉 사용자 쪽)을 향해야 함
#
# 메시 원본 방향을 모르므로 BP 에서 확인한 값을 기준으로 조정.
# Pitch = Y축 회전, Yaw = Z축 회전, Roll = X축 회전
#
# [중요] 아래 값은 초기 추정치입니다. 
# BP_DinoOverlay_Archelon 에서 거북이가 정면을 보는 회전값을 확인 후 여기에 입력하세요.
ROTATION_PITCH = 0.0    # Y축 회전 (위/아래 기울기)
ROTATION_YAW   = 180.0  # Z축 회전 (좌/우 방향) — 180도로 뒤집어 사용자 쪽 정면
ROTATION_ROLL  = 0.0    # X축 회전 (좌/우 기울기)

# --- 위치 (cm 단위) ---
# 부모(=액터 Root=AR 마커) 좌표계 기준.
# Rotation 에 영향을 받지 않으므로 직관적으로 조절 가능.
#   X+ = 마커 기준 오른쪽
#   Y+ = 마커 기준 앞쪽
#   Z+ = 마커 기준 위쪽
LOCATION_X = 0.0
LOCATION_Y = 0.0
LOCATION_Z = 0.0

# --- 스케일 ---
SCALE_X = 1.0
SCALE_Y = 1.0
SCALE_Z = 1.0

# ============================================================================
#  선택: BP 에서 현재 트랜스폼 읽어오기
# ============================================================================
# True 로 설정하면 BP_DinoOverlay_Archelon 에서 BoneMesh 의 현재 트랜스폼을
# 읽어와 위의 값 대신 그 값을 DA 에 기록한다.
# 즉 BP 에서 수동으로 잡아둔 트랜스폼을 자동으로 DA 에 복사한다.
COPY_FROM_BP = False

# ============================================================================
#  에셋 경로
# ============================================================================
DA_PATH = "/Game/UI/DinoCard/DA_Dino_Archelon"
BP_PATH = "/Game/Stuff/BluePrint/BP_DinoOverlay_Archelon.BP_DinoOverlay_Archelon"


# ============================================================================
#  BP 에서 트랜스폼 읽기
# ============================================================================
def read_bp_component_transform(component_name="BoneMesh"):
    """BP_DinoOverlay_Archelon 에서 지정 컴포넌트의 RelativeTransform 을 읽는다."""
    lib = unreal.EditorAssetLibrary

    bp_asset = lib.load_asset(BP_PATH)
    if bp_asset is None:
        unreal.log_error(f"[archelon] BP 로드 실패: {BP_PATH}")
        return None

    # SCS 노드에서 컴포넌트 템플릿 찾기
    scs = bp_asset.get_editor_property("simple_construction_script")
    if scs is None:
        unreal.log_error("[archelon] SCS 없음")
        return None

    for node in scs.get_all_nodes():
        template = node.get_editor_property("component_template")
        if template is None:
            continue
        if template.get_name() == component_name:
            loc = template.get_editor_property("relative_location")
            rot = template.get_editor_property("relative_rotation")
            scl = template.get_editor_property("relative_scale3d")
            unreal.log(
                f"[archelon] BP {component_name} 현재값:\n"
                f"   위치: X={loc.x:.2f}  Y={loc.y:.2f}  Z={loc.z:.2f}\n"
                f"   회전: P={rot.pitch:.2f}  Y={rot.yaw:.2f}  R={rot.roll:.2f}\n"
                f"   스케일: X={scl.x:.4f}  Y={scl.y:.4f}  Z={scl.z:.4f}"
            )
            return loc, rot, scl

    unreal.log_error(f"[archelon] BP 에서 '{component_name}' 컴포넌트를 찾지 못함")
    return None


# ============================================================================
#  DA 에 MeshTransform 기록
# ============================================================================
def write_da_mesh_transform(location, rotation, scale):
    """DA_Dino_Archelon 의 MeshTransform 을 설정한다."""
    lib = unreal.EditorAssetLibrary

    if not lib.does_asset_exist(DA_PATH):
        unreal.log_error(f"[archelon] DA 에셋 없음: {DA_PATH}")
        return False

    asset = lib.load_asset(DA_PATH)
    if asset is None:
        unreal.log_error(f"[archelon] DA 로드 실패: {DA_PATH}")
        return False

    # 현재 MeshTransform 출력
    current = asset.get_editor_property("MeshTransform")
    unreal.log(
        f"[archelon] DA 기존 MeshTransform:\n"
        f"   위치: {current.translation}\n"
        f"   회전: {current.rotation.rotator()}\n"
        f"   스케일: {current.scale3d}"
    )

    # 새 트랜스폼 구성
    new_transform = unreal.Transform(
        location=location,
        rotation=rotation,
        scale=scale,
    )

    asset.set_editor_property("MeshTransform", new_transform)
    lib.save_asset(DA_PATH)

    unreal.log(
        f"[archelon] ✅ DA 새 MeshTransform:\n"
        f"   위치: X={location.x:.2f}  Y={location.y:.2f}  Z={location.z:.2f}\n"
        f"   회전: P={rotation.pitch:.2f}  Y={rotation.yaw:.2f}  R={rotation.roll:.2f}\n"
        f"   스케일: X={scale.x:.4f}  Y={scale.y:.4f}  Z={scale.z:.4f}"
    )
    return True


# ============================================================================
#  실행
# ============================================================================
unreal.log("=" * 60)
unreal.log("[archelon] Archelon 오버레이 트랜스폼 조정 시작")
unreal.log("=" * 60)

# 항상 BP 의 현재 값을 참고로 출력
unreal.log("[archelon] --- BP 현재 트랜스폼 (참고용) ---")
bp_result = read_bp_component_transform("BoneMesh")

if COPY_FROM_BP and bp_result is not None:
    # BP 에서 읽은 값을 그대로 DA 에 복사
    bp_loc, bp_rot, bp_scl = bp_result
    unreal.log("[archelon] COPY_FROM_BP=True → BP 값을 DA 에 복사합니다")
    write_da_mesh_transform(bp_loc, bp_rot, bp_scl)
else:
    # 스크립트 상단에서 지정한 값 사용
    location = unreal.Vector(LOCATION_X, LOCATION_Y, LOCATION_Z)
    rotation = unreal.Rotator(ROTATION_PITCH, ROTATION_YAW, ROTATION_ROLL)
    scale    = unreal.Vector(SCALE_X, SCALE_Y, SCALE_Z)

    unreal.log(f"[archelon] 적용할 값:")
    unreal.log(f"   위치: X={LOCATION_X}  Y={LOCATION_Y}  Z={LOCATION_Z}")
    unreal.log(f"   회전: Pitch={ROTATION_PITCH}  Yaw={ROTATION_YAW}  Roll={ROTATION_ROLL}")
    unreal.log(f"   스케일: X={SCALE_X}  Y={SCALE_Y}  Z={SCALE_Z}")

    write_da_mesh_transform(location, rotation, scale)

unreal.log("=" * 60)
unreal.log("[archelon] 완료!")
unreal.log("=" * 60)
