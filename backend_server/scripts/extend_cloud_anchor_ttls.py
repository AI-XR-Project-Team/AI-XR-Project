#!/usr/bin/env python3
"""Cloud Anchor TTL 자동 연장 잡 (cron 일 1회 권장).

DB 의 활성 cloud_anchors 를 순회하며:
  1) Management API 로 현재 수명 조회 → DB 미러 필드 동기화(create/expire/max/last_localize)
  2) 곧 만료(기본 7일 이내)면 expireTime 을 max 까지 연장 (plan_extension)
  3) 404(만료·삭제)면 enabled=false 로 비활성화 (리졸브 목록서 제외)
  4) 연장 상한(max_expire_time, = create+365일)이 임박하면 재호스팅 필요로 경고

앱은 UE 무관하게 이 잡 덕에 "호스팅은 1일이지만 실제로는 계속 살아있는" 앵커를 쓴다.
드라이런:  python -m scripts.extend_cloud_anchor_ttls --dry-run
실제 실행:  SA_KEY_PATH=... python -m scripts.extend_cloud_anchor_ttls
배경: docs/nav-stage10-ttl-probe-runbook.md, 프로젝트 메모리 stage10_cloud_anchors.md.
"""
import argparse
import datetime as _dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal  # noqa: E402
from app.models.cloud_anchor import CloudAnchor  # noqa: E402
from app.services.cloud_anchor_admin import AnchorAdminClient, plan_extension  # noqa: E402

# 만료까지 이 기간 안이면 연장한다. max 상한이 이 기간 안이면 재호스팅 경고.
EXTEND_WITHIN = _dt.timedelta(days=7)


def _sync_mirror(row: CloudAnchor, info, now: _dt.datetime) -> None:
    row.create_time = info.create_time
    row.expire_time = info.expire_time
    row.max_expire_time = info.max_expire_time
    row.last_localize_time = info.last_localize_time
    row.ttl_synced_at = now


def main() -> int:
    ap = argparse.ArgumentParser(description="Cloud Anchor TTL 자동 연장 잡")
    ap.add_argument("--dry-run", action="store_true", help="연장/비활성화를 커밋하지 않고 계획만 출력")
    ap.add_argument("--within-days", type=int, default=EXTEND_WITHIN.days,
                    help=f"만료 임박 기준일 (기본 {EXTEND_WITHIN.days})")
    args = ap.parse_args()
    within = _dt.timedelta(days=args.within_days)
    now = _dt.datetime.now(_dt.timezone.utc)

    try:
        client = AnchorAdminClient()
    except FileNotFoundError as e:
        print(f"[에러] {e}")
        return 1

    db = SessionLocal()
    stats = {"scanned": 0, "extended": 0, "disabled": 0, "rehost": 0, "noop": 0, "error": 0}
    try:
        # 11단계 D5': cloud_id 가 NULL 인 행은 아직 스캔 안 한 '계획 포인트'다 — 건너뛴다.
        # D12 폴백: bind 시 즉시 연장이 실패해 ttl_extended_at 이 NULL 로 남은 행도
        # 여기서 자연히 집힌다(expire_time 이 곧 만료라 plan_extension 이 extend 를 낸다).
        rows = (
            db.query(CloudAnchor)
            .filter(CloudAnchor.enabled.is_(True), CloudAnchor.cloud_id.is_not(None))
            .all()
        )
        for row in rows:
            stats["scanned"] += 1
            status, info, txt = client.get(row.cloud_id)

            if status == 404:
                stats["disabled"] += 1
                print(f"[만료/삭제] {row.cloud_id} → enabled=false")
                if not args.dry_run:
                    row.enabled = False
                continue
            if status != 200 or info is None:
                stats["error"] += 1
                print(f"[에러] {row.cloud_id} HTTP {status}: {txt[:120]}")
                continue

            if not args.dry_run:
                _sync_mirror(row, info, now)

            plan = plan_extension(now, info.expire_time, info.max_expire_time, within)
            if plan.action == "extend":
                if args.dry_run:
                    print(f"[연장예정] {row.cloud_id} → {plan.target_expire.isoformat()}")
                    stats["extended"] += 1
                    continue
                pstatus, pinfo, ptxt = client.set_expire(row.cloud_id, plan.target_expire)
                if pstatus == 200 and pinfo is not None:
                    _sync_mirror(row, pinfo, now)
                    row.ttl_extended_at = now   # D12 — bind 때 실패했더라도 여기서 채워진다
                    stats["extended"] += 1
                    print(f"[연장] {row.cloud_id} → expire={pinfo.expire_time.isoformat()}")
                else:
                    stats["error"] += 1
                    print(f"[연장실패] {row.cloud_id} HTTP {pstatus}: {ptxt[:120]}")
            elif plan.action == "rehost_needed":
                stats["rehost"] += 1
                print(f"[재호스팅필요] {row.cloud_id} — {plan.reason} (label={row.label})")
            else:
                stats["noop"] += 1

        if not args.dry_run:
            db.commit()
    finally:
        db.close()

    print(f"\n요약: {stats}  (dry_run={args.dry_run})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
