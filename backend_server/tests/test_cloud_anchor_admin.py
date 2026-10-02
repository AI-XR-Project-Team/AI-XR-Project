"""TTL 연장 판정(plan_extension)과 RFC3339 파싱 단위 테스트.

네트워크·DB·google-auth 없이 순수 로직만 검증한다. 10단계 자동 연장 잡의 핵심 규칙:
- 만료 임박(within) + max 미달 → 연장
- max 상한 임박 → 재호스팅 필요(영구 소멸 전 경고)
- 여유 있음 → noop
"""
import datetime as _dt

from app.services.cloud_anchor_admin import (
    ExtensionPlan,
    fmt_rfc3339,
    parse_rfc3339,
    plan_extension,
)

NOW = _dt.datetime(2026, 9, 11, 0, 0, tzinfo=_dt.timezone.utc)
WITHIN = _dt.timedelta(days=7)


def _dt_days(n):
    return NOW + _dt.timedelta(days=n)


def test_extend_when_expiring_soon_and_below_max():
    max_t = _dt_days(300)
    plan = plan_extension(NOW, expire_time=_dt_days(1), max_expire_time=max_t, within=WITHIN)
    assert plan.action == "extend"
    assert plan.target_expire == max_t


def test_noop_when_plenty_of_time():
    plan = plan_extension(NOW, expire_time=_dt_days(90), max_expire_time=_dt_days(300), within=WITHIN)
    assert plan.action == "noop"


def test_rehost_needed_when_max_ceiling_imminent():
    # max 상한 자체가 within 안 → 더 늘려도 소용없음.
    plan = plan_extension(NOW, expire_time=_dt_days(1), max_expire_time=_dt_days(3), within=WITHIN)
    assert plan.action == "rehost_needed"


def test_no_extend_when_already_at_max():
    # expire 이 곧이지만 이미 max 와 같으면 연장 대상 아님(더 못 늘림).
    same = _dt_days(2)
    plan = plan_extension(NOW, expire_time=same, max_expire_time=same, within=WITHIN)
    # max - now = 2일 <= within(7일) 이므로 재호스팅 필요로 잡힌다.
    assert plan.action == "rehost_needed"


def test_unknown_when_missing_lifecycle():
    plan = plan_extension(NOW, expire_time=None, max_expire_time=None, within=WITHIN)
    assert plan.action == "unknown"


def test_rfc3339_roundtrip():
    dt = parse_rfc3339("2026-09-10T10:00:00Z")
    assert dt.year == 2026 and dt.month == 9 and dt.day == 10
    assert dt.tzinfo is not None
    assert fmt_rfc3339(dt) == "2026-09-10T10:00:00Z"


# --- 11단계 스키마 검증 (순수) ---
def test_point_upsert_defaults_match_stage11_rules():
    """계획 포인트 기본값이 11단계 결정과 일치해야 한다.

    D5'b — 앵커는 무조건 바닥(z=0). D8 — 설치 방위는 전부 +Y(90°).
    이 기본값이 바뀌면 CSV 에 z/heading 이 빠진 행을 시딩할 때 조용히 틀어진다.
    """
    from decimal import Decimal
    from app.schemas.cloud_anchor import CloudAnchorPointUpsert

    p = CloudAnchorPointUpsert(pos_x_cm=2280, pos_y_cm=300)
    assert p.pos_z_cm == Decimal("0")       # D5'b
    assert p.heading_deg == Decimal("90")   # D8
    assert p.enabled is True
    assert p.cloud_id is None if hasattr(p, "cloud_id") else True


def test_bind_requires_cloud_id_and_heading_optional():
    """bind 는 cloud_id 필수, heading 은 선택(생략 시 계획값 유지)."""
    import pytest as _pytest
    from decimal import Decimal
    from pydantic import ValidationError
    from app.schemas.cloud_anchor import CloudAnchorBind

    b = CloudAnchorBind(cloud_id="ua-1e53899ebe558a08f1d2f074939a990f")
    assert b.heading_deg is None

    b2 = CloudAnchorBind(cloud_id="ua-x", heading_deg=Decimal("87.4"))
    assert b2.heading_deg == Decimal("87.4")

    with _pytest.raises(ValidationError):
        CloudAnchorBind(cloud_id="")


def test_verify_latency_must_be_non_negative():
    import pytest as _pytest
    from pydantic import ValidationError
    from app.schemas.cloud_anchor import CloudAnchorVerify

    assert CloudAnchorVerify(ok=True, latency_ms=1800).latency_ms == 1800
    assert CloudAnchorVerify(ok=False).latency_ms is None
    with _pytest.raises(ValidationError):
        CloudAnchorVerify(ok=True, latency_ms=-1)


# --- D12: bind 직후 TTL 연장 예약 (순수) ---
def test_schedule_ttl_extension_noop_without_background():
    """background 가 없으면(테스트·직접호출) 조용히 넘어간다."""
    from app.services.cloud_anchor_ttl import schedule_ttl_extension

    schedule_ttl_extension(None, "row-id", "ua-x")  # 예외 없이 반환


def test_schedule_ttl_extension_queues_task():
    """bind 핸들러가 응답 후 실행될 태스크를 예약한다(응답을 막지 않는다)."""
    from fastapi import BackgroundTasks
    from app.services.cloud_anchor_ttl import _extend_now, schedule_ttl_extension

    bg = BackgroundTasks()
    schedule_ttl_extension(bg, "row-id", "ua-x")
    assert len(bg.tasks) == 1
    assert bg.tasks[0].func is _extend_now
    assert bg.tasks[0].args == ("row-id", "ua-x")


def test_extend_now_skips_without_credentials(monkeypatch):
    """SA_KEY_PATH 가 없으면 조용히 건너뛴다 — 예외를 던지면 안 된다.

    응답이 이미 나간 뒤라 잡을 곳이 없고, cron 폴백이 있기 때문이다.
    """
    from app.services import cloud_anchor_ttl

    monkeypatch.delenv("SA_KEY_PATH", raising=False)
    cloud_anchor_ttl._extend_now("row-id", "ua-x")  # 예외 없이 반환
