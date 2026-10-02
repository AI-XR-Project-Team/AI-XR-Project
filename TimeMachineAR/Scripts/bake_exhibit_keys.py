#!/usr/bin/env python3
"""DA_Dino_* 에셋의 ExhibitKey 를 서버 model_asset_key 값으로 채운다(재베이크).

도슨트 전시물 식별을 UUID → 안정 키(dinosaurs.model_asset_key)로 바꾸면서,
앱 DataAsset 이 들고 있던 옛 exhibits.id(UUID)를 안정 키 문자열로 덮어쓴다.
이 값은 DB 를 재시드해도, 서버 머신이 바뀌어도 변하지 않으므로 다시는
"도슨트 연결 실패 / 어떤 공룡인지 모름"이 나지 않는다.

이건 **UE 에디터 파이썬 스크립트**다(일반 파이썬이 아니라 `import unreal`).
C++ 에서 ExhibitId→ExhibitKey 개명을 반영한 에디터가 먼저 빌드돼 있어야 한다
(프로퍼티가 존재해야 set 할 수 있다).

실행(헤드리스, 리포 루트에서):
  "<UE>/Engine/Binaries/Mac/UnrealEditor-Cmd" \
    "<repo>/TimeMachineAR/TimeMachineAR.uproject" \
    -run=pythonscript -script="<repo>/docs/bake-exhibit-keys.py" \
    -unattended -nosplash

여러 번 돌려도 안전하다(멱등). 값이 이미 맞으면 저장을 건너뛴다.
"""
import unreal

# DA 경로 → 서버 dinosaurs.model_asset_key (안정 자연키).
# 값은 서버 seed(backend_server) 기준이며, 서버 DB 조회로도 확인했다:
#   trex_full_skeleton / triceratops_full_skeleton /
#   ankylosaurus_full_skeleton / brachiosaurus_full_skeleton
MAPPING = {
    "/Game/UI/DinoCard/DA_Dino_TRex": "trex_full_skeleton",
    "/Game/UI/DinoCard/DA_Dino_Triceratops": "triceratops_full_skeleton",
    "/Game/UI/DinoCard/DA_Dino_Ankylosaurus": "ankylosaurus_full_skeleton",
    "/Game/UI/DinoCard/DA_Dino_Brachiosaurus": "brachiosaurus_full_skeleton",
}

lib = unreal.EditorAssetLibrary
changed = []
missing = []

for path, key in MAPPING.items():
    if not lib.does_asset_exist(path):
        unreal.log_warning(f"[bake] 에셋 없음: {path}")
        missing.append(path)
        continue

    asset = lib.load_asset(path)
    if asset is None:
        unreal.log_warning(f"[bake] 로드 실패: {path}")
        missing.append(path)
        continue

    current = asset.get_editor_property("ExhibitKey")
    if current == key:
        unreal.log(f"[bake] = {path}: 이미 {key!r} — 건너뜀")
        continue

    asset.set_editor_property("ExhibitKey", key)
    lib.save_asset(path)
    unreal.log(f"[bake] + {path}: ExhibitKey {current!r} -> {key!r}")
    changed.append(path)

unreal.log(f"[bake] 완료. 변경 {len(changed)}개, 문제 {len(missing)}개.")
if missing:
    # 헤드리스 실행에서 실패를 눈에 띄게 남긴다.
    unreal.log_error(f"[bake] 처리 못한 에셋: {missing}")
