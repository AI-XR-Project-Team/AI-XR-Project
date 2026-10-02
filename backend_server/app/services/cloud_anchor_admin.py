"""ARCore Cloud Anchor Management API 클라이언트 + TTL 연장 판정.

서비스계정 JSON → OAuth(arcore.management) → 앵커 조회/TTL연장/조기만료.
CLI(scripts/arcore_anchor_admin.py)와 자동 연장 잡(scripts/extend_cloud_anchor_ttls.py)이
공용으로 쓴다. 네트워크에 의존하지 않는 판정 로직(plan_extension)은 순수 함수라 단위 테스트된다.

배경/판정 근거: docs/nav-stage10-ttl-probe-runbook.md, 프로젝트 메모리 stage10_cloud_anchors.md.
의존: google-auth, requests  (pip install --break-system-packages google-auth requests)
"""
from __future__ import annotations

import datetime as _dt
import os
from dataclasses import dataclass
from typing import Optional

SCOPE = "https://www.googleapis.com/auth/arcore.management"
_BASE = "https://arcore.googleapis.com/v1beta2/management/anchors"


# --------------------------------------------------------------------------- #
# 순수 로직 (네트워크·DB 무관, 단위 테스트 대상)
# --------------------------------------------------------------------------- #
def parse_rfc3339(s: str) -> _dt.datetime:
    """ARCore 가 주는 RFC3339(끝 Z) 문자열을 tz-aware datetime 으로."""
    return _dt.datetime.fromisoformat(s.replace("Z", "+00:00"))


def fmt_rfc3339(dt: _dt.datetime) -> str:
    return dt.astimezone(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


@dataclass(frozen=True)
class ExtensionPlan:
    action: str            # "extend" | "noop" | "rehost_needed" | "unknown"
    target_expire: Optional[_dt.datetime] = None
    reason: str = ""


def plan_extension(
    now: _dt.datetime,
    expire_time: Optional[_dt.datetime],
    max_expire_time: Optional[_dt.datetime],
    within: _dt.timedelta,
) -> ExtensionPlan:
    """곧 만료될 앵커의 TTL 을 max 까지 밀지 결정한다.

    - max_expire_time 자체가 `within` 안이면 더 늘려도 의미 없음 → 재호스팅 필요.
      (max_expire_time = create_time + 365일 고정이라 이 지점을 넘기면 앵커는 영구 소멸.)
    - expire_time 이 `within` 안이고 아직 max 보다 이르면 → max 로 연장.
    - 그 외 → 아무것도 안 함.
    """
    if expire_time is None or max_expire_time is None:
        return ExtensionPlan("unknown", None, "수명 정보 없음(먼저 sync 필요)")
    if max_expire_time - now <= within:
        return ExtensionPlan(
            "rehost_needed", None,
            f"max_expire_time 이 {within} 이내 — 연장 상한 도달, 재호스팅 필요",
        )
    if expire_time - now < within and expire_time < max_expire_time:
        return ExtensionPlan("extend", max_expire_time, "곧 만료 → max 로 연장")
    return ExtensionPlan("noop", None, "여유 있음")


# --------------------------------------------------------------------------- #
# 네트워크 클라이언트
# --------------------------------------------------------------------------- #
@dataclass
class AnchorInfo:
    name: str
    create_time: Optional[_dt.datetime]
    expire_time: Optional[_dt.datetime]
    max_expire_time: Optional[_dt.datetime]
    last_localize_time: Optional[_dt.datetime]
    raw: dict

    @classmethod
    def from_json(cls, d: dict) -> "AnchorInfo":
        def g(k):
            return parse_rfc3339(d[k]) if d.get(k) else None
        return cls(
            name=d.get("name", ""),
            create_time=g("createTime"),
            expire_time=g("expireTime"),
            max_expire_time=g("maximumExpireTime"),
            last_localize_time=g("lastLocalizeTime"),
            raw=d,
        )


class AnchorAdminClient:
    """arcore.management API 호출 래퍼. 서비스계정 JSON 으로 인증한다."""

    def __init__(self, sa_key_path: Optional[str] = None):
        sa_path = sa_key_path or os.environ.get("SA_KEY_PATH")
        if not sa_path or not os.path.exists(sa_path):
            raise FileNotFoundError(
                "서비스계정 JSON 경로가 필요하다(SA_KEY_PATH 환경변수 또는 인자)."
            )
        # import 를 여기서 — google-auth 미설치 환경에서도 순수 로직은 import 되게.
        from google.oauth2 import service_account
        from google.auth.transport.requests import AuthorizedSession

        creds = service_account.Credentials.from_service_account_file(
            sa_path, scopes=[SCOPE]
        )
        self._session = AuthorizedSession(creds)

    def _url(self, anchor_id: str) -> str:
        return f"{_BASE}/{anchor_id}"

    def get(self, anchor_id: str):
        """(status_code, AnchorInfo|None, raw_text) 반환."""
        r = self._session.get(self._url(anchor_id))
        if r.status_code == 200:
            return r.status_code, AnchorInfo.from_json(r.json()), r.text
        return r.status_code, None, r.text

    def set_expire(self, anchor_id: str, when: _dt.datetime):
        """expireTime 을 지정 시각으로 PATCH. (status_code, AnchorInfo|None, raw_text)."""
        r = self._session.patch(
            self._url(anchor_id),
            params={"updateMask": "expire_time"},
            json={"expireTime": fmt_rfc3339(when)},
        )
        if r.status_code == 200:
            return r.status_code, AnchorInfo.from_json(r.json()), r.text
        return r.status_code, None, r.text

    def extend_to_max(self, anchor_id: str):
        """현재 max_expire_time 까지 expireTime 을 밀어 올린다."""
        status, info, txt = self.get(anchor_id)
        if status != 200 or info is None or info.max_expire_time is None:
            return status, None, txt
        return self.set_expire(anchor_id, info.max_expire_time)
