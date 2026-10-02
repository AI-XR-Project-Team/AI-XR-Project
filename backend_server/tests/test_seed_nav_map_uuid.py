"""seed_nav 의 맵 UUID 결정성 계약 테스트.

`map_spaces.id` 를 예전처럼 DB 의 gen_random_uuid() 로 만들면 시드하는 PC 마다
맵 UUID 가 달라져, 앱 DefaultGame.ini 의 DefaultMapId 가 남의 DB 값이 되어
네비게이션이 전부 404 로 죽는다. 그래서 seed_nav 는 map_key 로부터 uuid5 로
맵 UUID 를 결정적으로 유도한다. 이 값이 바뀌면 배포된 앱의 DefaultMapId 계약이
깨지므로, 아래 상수들을 고정해 회귀를 막는다.
"""
import importlib

seed_nav = importlib.import_module("scripts.seed_nav")


# 앱 DefaultGame.ini / RUNNING.md 에 박아 둔 값과 반드시 일치해야 하는 고정 UUID.
_EXPECTED = {
    "floor1": "1cdb729e-0e7f-555c-99cf-6b83eb973e7b",
    "neuti4": "341e5556-333e-5dc1-b43b-5df649a830bd",
    "neuti4f": "385f15c8-bf2c-58ec-a2d7-8db2aa34ac0b",
}


def test_map_uuid_matches_committed_contract():
    """map_key → 고정 UUID. 값이 바뀌면 배포된 APK 의 DefaultMapId 가 깨진다."""
    for key, expected in _EXPECTED.items():
        assert str(seed_nav._map_uuid(key)) == expected, (
            f"map_key '{key}' 의 결정적 UUID 가 바뀌었다. "
            f"_MAP_ID_NAMESPACE 를 되돌리거나 앱 DefaultMapId·RUNNING.md 를 함께 고쳐라."
        )


def test_map_uuid_is_deterministic_and_key_specific():
    # 같은 키는 항상 같은 값(머신 무관).
    assert seed_nav._map_uuid("neuti4f") == seed_nav._map_uuid("neuti4f")
    # 공백은 무시(CSV 파싱 여유).
    assert seed_nav._map_uuid(" neuti4f ") == seed_nav._map_uuid("neuti4f")
    # 키가 다르면 값도 달라야 한다.
    assert seed_nav._map_uuid("neuti4") != seed_nav._map_uuid("neuti4f")
