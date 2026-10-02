#!/usr/bin/env python3
"""지도 계획 포인트 시딩 CLI — 11단계 D5'.

좌표는 지도에서 사전 확정하고(이 스크립트), 현장에서 cloud_id 를 바인딩한다(관리자 앱).
입력 원본은 `docs/nav-stage11/cloud-anchor-plan.csv`:

    point_no,edge,pos_x_cm,pos_y_cm,pos_z_cm,heading_deg,note

멱등하다 — 같은 point_no 를 다시 넣으면 좌표만 덮어쓰고 **cloud_id 는 보존**한다
(좌표 오타를 고칠 때 이미 스캔한 앵커를 잃지 않기 위해서다).

실행 (backend_server/ 를 작업 디렉터리로):
    python -m scripts.seed_cloud_anchor_points \
        --map <map_uuid> --csv ../docs/nav-stage11/cloud-anchor-plan.csv
    python -m scripts.seed_cloud_anchor_points --map <uuid> --csv ... --dry-run
"""
import argparse
import csv
import os
import sys
import uuid
from decimal import Decimal

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal  # noqa: E402
from app.models.cloud_anchor import CloudAnchor  # noqa: E402
from app.models.map_space import MapSpace  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="Cloud Anchor 계획 포인트 시딩")
    ap.add_argument("--map", required=True, help="map_spaces.id (UUID)")
    ap.add_argument("--csv", required=True, help="cloud-anchor-plan.csv 경로")
    ap.add_argument("--dry-run", action="store_true", help="쓰지 않고 무엇이 바뀔지만 출력")
    args = ap.parse_args()

    try:
        map_id = uuid.UUID(args.map)
    except ValueError:
        print(f"[에러] --map 이 UUID 가 아니다: {args.map}")
        return 1
    if not os.path.exists(args.csv):
        print(f"[에러] CSV 없음: {args.csv}")
        return 1

    with open(args.csv, newline="", encoding="utf-8") as f:
        plan = list(csv.DictReader(f))
    if not plan:
        print("[에러] CSV 가 비었다")
        return 1

    db = SessionLocal()
    try:
        if db.get(MapSpace, map_id) is None:
            print(f"[에러] map_id 없음: {map_id}")
            return 1

        created = updated = 0
        for r in plan:
            point_no = int(r["point_no"])
            row = (
                db.query(CloudAnchor)
                .filter(CloudAnchor.map_id == map_id,
                        CloudAnchor.point_no == point_no)
                .one_or_none()
            )
            is_new = row is None
            if args.dry_run:
                state = "신규" if is_new else ("갱신(바인딩 보존)" if row.cloud_id else "갱신")
                print(f"[{state}] #{point_no} {r.get('edge','')} "
                      f"({r['pos_x_cm']},{r['pos_y_cm']},{r['pos_z_cm']}) "
                      f"heading={r['heading_deg']}")
                created += is_new
                updated += not is_new
                continue

            if row is None:
                row = CloudAnchor(map_id=map_id, point_no=point_no)
                db.add(row)
                created += 1
            else:
                updated += 1
            row.pos_x_cm = Decimal(r["pos_x_cm"])
            row.pos_y_cm = Decimal(r["pos_y_cm"])
            row.pos_z_cm = Decimal(r["pos_z_cm"])
            row.heading_deg = Decimal(r["heading_deg"])
            row.edge = (r.get("edge") or None)
            row.label = (r.get("edge") or None)   # 관리자 앱 목록 표시용
            row.note = (r.get("note") or None)[:200] if r.get("note") else None
            row.enabled = True
            # cloud_id 는 손대지 않는다 — 이미 스캔한 앵커를 잃지 않기 위해.

        if args.dry_run:
            print(f"\n[dry-run] 신규 {created} · 갱신 {updated} (쓰지 않음)")
            return 0
        db.commit()
        print(f"[완료] 신규 {created} · 갱신 {updated} — map={map_id}")
        return 0
    finally:
        db.close()


if __name__ == "__main__":
    raise SystemExit(main())
