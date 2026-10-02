"""markers.csv → DB 마커 좌표/헤딩/노트 갱신 (멱등).

seed_nav.py 의 마커 적재는 code(자연키) 존재 시 건너뛰기만 하고 좌표를
갱신하지 않는다(삽입 전용 멱등). 이미 등록된 마커의 물리 위치를 바꾸면
CSV 만 고쳐서는 DB 에 반영되지 않으므로, 이 스크립트로 기존 행을 CSV 값에
맞춰 UPDATE 한다. 값이 이미 같으면 건너뛴다(멱등, 여러 번 돌려도 안전).

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/sync_markers.py            # 모든 마커 동기화
    python scripts/sync_markers.py EX3-TREX EX2-BRACHIO   # 특정 code 만

CSV 에만 있고 DB 에 없는 마커는 여기서 만들지 않는다(그건 seed_nav 의 몫).
"""
import csv
import os
import sys
from decimal import Decimal

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal          # noqa: E402
from app.models.marker import Marker          # noqa: E402

SEED_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "seeds", "nav"
)


def _dec(v):
    v = (v or "").strip()
    return Decimal(v) if v else Decimal("0")


def sync(only_codes=None):
    only = {c.strip() for c in only_codes} if only_codes else None
    path = os.path.join(SEED_DIR, "markers.csv")
    changed = skipped = missing = 0

    db = SessionLocal()
    try:
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                code = row["code"].strip()
                if only is not None and code not in only:
                    continue

                m = db.query(Marker).filter_by(code=code).first()
                if m is None:
                    print(f"[!] DB 에 없음 (seed_nav 로 먼저 삽입): {code}")
                    missing += 1
                    continue

                new = dict(
                    marker_type=(row.get("marker_type") or "").strip() or "qr",
                    pos_x_cm=_dec(row["pos_x_cm"]),
                    pos_y_cm=_dec(row["pos_y_cm"]),
                    pos_z_cm=_dec(row["pos_z_cm"]),
                    heading_deg=_dec(row.get("heading_deg")),
                    note=(row.get("note") or "").strip() or None,
                )
                diffs = {k: (getattr(m, k), v) for k, v in new.items()
                         if getattr(m, k) != v}
                if not diffs:
                    print(f"[=] 변경 없음: {code}")
                    skipped += 1
                    continue

                for k, v in new.items():
                    setattr(m, k, v)
                where = ", ".join(f"{k} {a}->{b}" for k, (a, b) in diffs.items())
                print(f"[+] 갱신: {code}  ({where})")
                changed += 1

        db.commit()
    finally:
        db.close()

    print(f"\n완료 — 갱신 {changed}, 변경없음 {skipped}, 미존재 {missing}")


if __name__ == "__main__":
    sync(sys.argv[1:] or None)
