#!/usr/bin/env python3
"""수동 바인딩 CLI — 계획 포인트에 cloud_id 를 붙인다 (11단계 D5').

좌표 시딩은 `scripts/seed_cloud_anchor_points.py`(CSV) 가 먼저 한다.
이 스크립트는 관리자 앱 없이 손으로 바인딩할 때, 그리고 실험 앵커를 꽂아볼 때 쓴다.
관리자 앱은 같은 일을 `PUT /maps/{map}/cloud-anchors/points/{n}/bind` 로 한다.

--extend 를 주면 바인딩과 동시에 Management API 로 TTL 을 max(365일)까지 밀고
수명 미러를 채운다(서버 라우터는 이걸 백그라운드로 한다 — D12).

실행 (backend_server/ 를 작업 디렉터리로):
    python -m scripts.seed_cloud_anchor --map <uuid> --point-no 3 --cloud-id ua-... [--extend]
"""
import argparse
import datetime as _dt
import os
import sys
import uuid
from decimal import Decimal  # noqa: F401  (heading 인자 파싱에 쓴다)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal  # noqa: E402
from app.models.cloud_anchor import CloudAnchor  # noqa: E402
from app.models.map_space import MapSpace  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="Cloud Anchor 수동 바인딩")
    ap.add_argument("--map", required=True, help="map_spaces.id (UUID)")
    ap.add_argument("--point-no", type=int, required=True, help="계획 포인트 번호")
    ap.add_argument("--cloud-id", required=True)
    ap.add_argument("--heading", type=Decimal, default=None,
                    help="실측 heading_deg (생략 시 계획값 유지)")
    ap.add_argument("--note", default=None)
    ap.add_argument("--extend", action="store_true",
                    help="바인딩과 동시에 TTL 을 max 로 연장하고 수명 미러 동기화")
    args = ap.parse_args()

    try:
        map_id = uuid.UUID(args.map)
    except ValueError:
        print(f"[에러] --map 이 UUID 가 아니다: {args.map}")
        return 1

    db = SessionLocal()
    try:
        if db.get(MapSpace, map_id) is None:
            print(f"[에러] map_id 없음: {map_id}")
            return 1

        row = (
            db.query(CloudAnchor)
            .filter(CloudAnchor.map_id == map_id,
                    CloudAnchor.point_no == args.point_no)
            .one_or_none()
        )
        if row is None:
            print(f"[에러] 계획 포인트 #{args.point_no} 가 없다. "
                  f"먼저 seed_cloud_anchor_points.py 로 좌표를 시딩할 것.")
            return 1

        clash = (
            db.query(CloudAnchor)
            .filter(CloudAnchor.cloud_id == args.cloud_id)
            .one_or_none()
        )
        if clash is not None and clash.id != row.id:
            print(f"[에러] cloud_id 가 포인트 #{clash.point_no} 에 이미 바인딩됨")
            return 1

        row.cloud_id = args.cloud_id
        if args.heading is not None:
            row.heading_deg = args.heading
        if args.note is not None:
            row.note = args.note
        # 재바인딩이면 이전 검증·수명 기록은 다른 앵커의 것이다.
        row.verified_at = None
        row.verify_latency_ms = None
        row.ttl_extended_at = None
        db.commit()
        print(f"[바인딩] #{args.point_no} ← {args.cloud_id} "
              f"({row.pos_x_cm},{row.pos_y_cm},{row.pos_z_cm}) heading={row.heading_deg}")

        if args.extend:
            from app.services.cloud_anchor_admin import AnchorAdminClient
            client = AnchorAdminClient()
            status, info, txt = client.extend_to_max(args.cloud_id)
            if status != 200 or info is None:
                print(f"[연장실패] HTTP {status}: {txt[:160]}")
                print("  → cron 잡(extend_cloud_anchor_ttls.py)이 재시도한다.")
                return 1
            now = _dt.datetime.now(_dt.timezone.utc)
            row.create_time = info.create_time
            row.expire_time = info.expire_time
            row.max_expire_time = info.max_expire_time
            row.last_localize_time = info.last_localize_time
            row.ttl_synced_at = now
            row.ttl_extended_at = now
            db.commit()
            print(f"[연장] expire={info.expire_time} (max={info.max_expire_time})")
        return 0
    finally:
        db.close()


if __name__ == "__main__":
    raise SystemExit(main())
