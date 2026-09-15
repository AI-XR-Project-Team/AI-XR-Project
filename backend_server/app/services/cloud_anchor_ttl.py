"""bind 직후 TTL 즉시 연장 (11단계 D12).

앱이 `hostCloudAnchorAsync(ttlDays=1)` 로 호스팅하므로 **앵커는 24시간 뒤 사라진다.**
서버가 Management API 로 max(=create+365일)까지 밀어 올려야 살아남는다.

D12 — 이걸 **응답 후 백그라운드로** 건다:
  * 앱 체감 지연 0 — 관리자가 현장에서 구글 왕복을 기다리지 않는다
  * 그래도 카운트다운은 초 단위 안에 끊긴다
  * 실패해도 bind 자체는 성공이다 → `ttl_extended_at` 이 NULL 로 남고
    `scripts/extend_cloud_anchor_ttls.py` cron 잡이 픽업한다.
    워커가 죽어 태스크가 유실돼도 같은 경로로 복구되므로 유실이 치명적이지 않다.

자격증명(SA_KEY_PATH)이 없는 환경 — 로컬 개발·CI·테스트 — 에서는 조용히 건너뛴다.
연장은 cron 이 맡으면 되고, 여기서 예외를 던지면 bind 응답이 이미 나간 뒤라 잡을 곳이 없다.
"""
from __future__ import annotations

import datetime as _dt
import logging
import os
import uuid as _uuid
from typing import Optional

from fastapi import BackgroundTasks

log = logging.getLogger(__name__)


def _extend_now(anchor_row_id: str, cloud_id: str) -> None:
    """백그라운드 본체 — Management API 로 TTL 을 max 까지 밀고 수명 미러를 채운다.

    어떤 예외도 밖으로 내보내지 않는다(응답은 이미 나갔고, cron 폴백이 있다).
    """
    if not os.environ.get("SA_KEY_PATH"):
        log.info("cloud-anchor TTL 연장 건너뜀(SA_KEY_PATH 없음) — cron 이 처리한다: %s",
                 cloud_id)
        return

    from app.core.db import SessionLocal
    from app.models.cloud_anchor import CloudAnchor
    from app.services.cloud_anchor_admin import AnchorAdminClient

    db = None
    try:
        client = AnchorAdminClient()
        status, info, txt = client.extend_to_max(cloud_id)
        if status != 200 or info is None:
            log.warning("cloud-anchor TTL 연장 실패(cron 이 재시도) %s: %s %s",
                        cloud_id, status, txt[:200])
            return

        db = SessionLocal()
        row = db.get(CloudAnchor, _uuid.UUID(anchor_row_id))
        if row is None:
            log.warning("TTL 연장 후 행을 못 찾음(삭제됨?): %s", anchor_row_id)
            return
        now = _dt.datetime.now(_dt.timezone.utc)
        row.create_time = info.create_time
        row.expire_time = info.expire_time
        row.max_expire_time = info.max_expire_time
        row.last_localize_time = info.last_localize_time
        row.ttl_synced_at = now
        row.ttl_extended_at = now
        db.commit()
        log.info("cloud-anchor TTL 연장 완료 %s → %s", cloud_id, info.expire_time)
    except Exception:  # noqa: BLE001 — 백그라운드라 삼키고 cron 에 맡긴다
        log.exception("cloud-anchor TTL 연장 중 예외(cron 이 재시도): %s", cloud_id)
    finally:
        if db is not None:
            db.close()


def schedule_ttl_extension(
    background: Optional[BackgroundTasks], anchor_row_id: str, cloud_id: str
) -> None:
    """bind 핸들러에서 호출 — 응답을 보낸 뒤 `_extend_now` 가 돌게 예약한다.

    `background` 가 None 이면(테스트 등) 아무것도 하지 않는다.
    """
    if background is None:
        return
    background.add_task(_extend_now, anchor_row_id, cloud_id)
