"""Cloud Anchors 라우터 — 11단계 포인트 우선(D5') 계약.

흐름 (docs/specs/feature-ue-nav-stage11.md §3.1, docs/nav-stage11-admin-mode.md §2):

    ① 계획 시딩   PUT  /maps/{map}/cloud-anchors/points/{point_no}   ← 좌표만, cloud_id 없음
    ② 현장 등록   PUT  /maps/{map}/cloud-anchors/points/{n}/bind     ← cloud_id + 실측 heading
                  → D12: 응답은 즉시 반환하고, TTL 365일 연장은 백그라운드로 건다
    ③ 검증 기록   POST /maps/{map}/cloud-anchors/points/{n}/verify   ← 앱 재시작 후 리졸브 결과
    ④ 리졸브 목록 GET  /maps/{map}/cloud-anchors                     ← 기본 bound 만

⚠️ 쓰기 엔드포인트는 관리자용인데 이 프로젝트엔 인증 계층이 없어 열려 있다.
   운영 배포 시 내부망/토큰 보호를 붙여야 한다(11단계는 프로토타입이라 보류).
"""
import uuid
from typing import List, Optional

from fastapi import APIRouter, BackgroundTasks, Depends, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.cloud_anchor import CloudAnchor
from app.models.map_space import MapSpace
from app.schemas.cloud_anchor import (CloudAnchorBind, CloudAnchorPointUpsert,
                                      CloudAnchorRead, CloudAnchorVerify)
from app.services.cloud_anchor_ttl import schedule_ttl_extension

router = APIRouter()

_BASE = "/maps/{map_id}/cloud-anchors"


def _to_read(row: CloudAnchor) -> CloudAnchorRead:
    return CloudAnchorRead(
        point_no=row.point_no,
        edge=row.edge,
        cloud_id=row.cloud_id,
        pos_x_cm=row.pos_x_cm,
        pos_y_cm=row.pos_y_cm,
        pos_z_cm=row.pos_z_cm,
        heading_deg=row.heading_deg,
        label=row.label,
        is_bound=row.cloud_id is not None,
        is_verified=row.verified_at is not None,
    )


def _require_map(db: Session, map_id: uuid.UUID) -> None:
    if db.get(MapSpace, map_id) is None:
        raise HTTPException(status_code=404, detail="map not found")


def _require_point(db: Session, map_id: uuid.UUID, point_no: int) -> CloudAnchor:
    row = db.scalar(
        select(CloudAnchor).where(
            CloudAnchor.map_id == map_id, CloudAnchor.point_no == point_no
        )
    )
    if row is None:
        raise HTTPException(status_code=404, detail="point not found")
    return row


@router.get(
    _BASE,
    response_model=List[CloudAnchorRead],
    summary="Cloud Anchor 목록 조회",
    responses={404: {"description": "map_id 없음"}},
)
def list_cloud_anchors(
    map_id: uuid.UUID,
    state: str = Query(
        "bound",
        pattern="^(bound|unbound|all)$",
        description="bound=리졸브 가능(기본, 방문객 앱용) / unbound=미등록 포인트(관리자 앱용) / all",
    ),
    db: Session = Depends(get_db),
) -> List[CloudAnchorRead]:
    """state 로 용도를 가른다.

    - `bound`(기본): cloud_id 가 붙은 enabled 앵커 → 방문객 앱의 리졸브 대상 집합.
      **기본값이 bound 인 이유**: 미바인딩 포인트를 방문객 앱에 흘리면 리졸브할 수 없는
      cloud_id=null 행을 받게 된다.
    - `unbound`: 아직 스캔하지 않은 포인트 → 관리자 앱의 작업 목록.
    - `all`: 진행 현황 확인용.
    """
    _require_map(db, map_id)
    stmt = select(CloudAnchor).where(CloudAnchor.map_id == map_id)
    if state == "bound":
        stmt = stmt.where(CloudAnchor.cloud_id.is_not(None),
                          CloudAnchor.enabled.is_(True))
    elif state == "unbound":
        stmt = stmt.where(CloudAnchor.cloud_id.is_(None))
    return [_to_read(r) for r in db.scalars(stmt.order_by(CloudAnchor.point_no)).all()]


@router.put(
    _BASE + "/points/{point_no}",
    response_model=CloudAnchorRead,
    summary="계획 포인트 등록/갱신 (좌표 시딩)",
    responses={404: {"description": "map_id 없음"}},
)
def upsert_point(
    map_id: uuid.UUID,
    point_no: int,
    body: CloudAnchorPointUpsert,
    db: Session = Depends(get_db),
) -> CloudAnchorRead:
    """지도 계획의 포인트를 넣는다(멱등). cloud_id 는 건드리지 않는다.

    이미 바인딩된 포인트에 다시 호출해도 **cloud_id 는 보존**된다 — 좌표 오타를 고칠 때
    앵커를 잃지 않기 위해서다. 앵커를 떼려면 unbind 를 쓴다.
    """
    _require_map(db, map_id)
    row = db.scalar(
        select(CloudAnchor).where(
            CloudAnchor.map_id == map_id, CloudAnchor.point_no == point_no
        )
    )
    if row is None:
        row = CloudAnchor(map_id=map_id, point_no=point_no)
        db.add(row)
    row.pos_x_cm = body.pos_x_cm
    row.pos_y_cm = body.pos_y_cm
    row.pos_z_cm = body.pos_z_cm
    row.heading_deg = body.heading_deg
    row.edge = body.edge
    row.label = body.label
    row.note = body.note
    row.enabled = body.enabled
    db.commit()
    db.refresh(row)
    return _to_read(row)


@router.put(
    _BASE + "/points/{point_no}/bind",
    response_model=CloudAnchorRead,
    summary="현장 등록 — cloud_id 바인딩 (+ TTL 즉시 연장)",
    responses={404: {"description": "map_id 또는 point_no 없음"},
               409: {"description": "cloud_id 가 다른 포인트에 이미 바인딩됨"}},
)
def bind_point(
    map_id: uuid.UUID,
    point_no: int,
    body: CloudAnchorBind,
    background: BackgroundTasks,
    db: Session = Depends(get_db),
) -> CloudAnchorRead:
    """호스팅 성공 직후 관리자 앱이 호출한다. 응답은 **기다리지 않고 즉시** 돌아간다.

    D12 — TTL 연장은 `BackgroundTasks` 로 응답 후에 건다:
      * 앱 체감 지연 0 (구글 Management API 왕복을 앱이 기다리지 않는다)
      * 그래도 24h 카운트다운은 초 단위 안에 끊긴다
      * 실패하면 `ttl_extended_at` 이 NULL 로 남고 cron 잡이 픽업한다
        (워커가 죽어 백그라운드 태스크가 유실돼도 동일하게 복구된다)

    재바인딩(재호스팅으로 cloud_id 가 새로 발급된 경우)도 같은 엔드포인트로 덮어쓴다.
    """
    _require_map(db, map_id)
    row = _require_point(db, map_id, point_no)

    clash = db.scalar(
        select(CloudAnchor).where(CloudAnchor.cloud_id == body.cloud_id)
    )
    if clash is not None and clash.id != row.id:
        raise HTTPException(
            status_code=409,
            detail=f"cloud_id already bound to point {clash.point_no}",
        )

    row.cloud_id = body.cloud_id
    if body.heading_deg is not None:
        row.heading_deg = body.heading_deg
    if body.note is not None:
        row.note = body.note
    # 재바인딩이면 이전 검증·수명 기록은 무효다(다른 앵커다).
    row.verified_at = None
    row.verify_latency_ms = None
    row.ttl_extended_at = None
    db.commit()
    db.refresh(row)

    schedule_ttl_extension(background, str(row.id), body.cloud_id)
    return _to_read(row)


@router.post(
    _BASE + "/points/{point_no}/verify",
    response_model=CloudAnchorRead,
    summary="검증 결과 기록 — 앱 재시작 후 리졸브",
    responses={404: {"description": "map_id 또는 point_no 없음"},
               409: {"description": "아직 바인딩되지 않은 포인트"}},
)
def verify_point(
    map_id: uuid.UUID,
    point_no: int,
    body: CloudAnchorVerify,
    db: Session = Depends(get_db),
) -> CloudAnchorRead:
    """11단계 §4① — 앱을 재시작한 뒤 리졸브해 본 결과를 기록한다.

    실패(ok=false)면 `verified_at` 을 지우고 `enabled=false` 로 내려 방문객 목록에서 빼고
    재등록 대상으로 남긴다. 행은 지우지 않는다(현장 이력 보존).
    """
    _require_map(db, map_id)
    row = _require_point(db, map_id, point_no)
    if row.cloud_id is None:
        raise HTTPException(status_code=409, detail="point is not bound yet")

    if body.ok:
        from datetime import datetime, timezone
        row.verified_at = datetime.now(timezone.utc)
        row.verify_latency_ms = body.latency_ms
        row.enabled = True
    else:
        row.verified_at = None
        row.verify_latency_ms = body.latency_ms
        row.enabled = False
    db.commit()
    db.refresh(row)
    return _to_read(row)


@router.delete(
    _BASE + "/points/{point_no}/bind",
    response_model=CloudAnchorRead,
    summary="바인딩 해제 (재스캔용)",
    responses={404: {"description": "map_id 또는 point_no 없음"}},
)
def unbind_point(
    map_id: uuid.UUID,
    point_no: int,
    db: Session = Depends(get_db),
) -> CloudAnchorRead:
    """cloud_id 만 떼고 포인트(좌표)는 남긴다 → 그 자리를 다시 스캔할 수 있다.

    구글 쪽 앵커는 지우지 않는다(만료되게 둔다). 재호스팅하면 cloud_id 가 새로 발급된다.
    """
    _require_map(db, map_id)
    row = _require_point(db, map_id, point_no)
    row.cloud_id = None
    row.verified_at = None
    row.verify_latency_ms = None
    row.ttl_extended_at = None
    row.create_time = row.expire_time = row.max_expire_time = None
    row.last_localize_time = row.ttl_synced_at = None
    db.commit()
    db.refresh(row)
    return _to_read(row)
