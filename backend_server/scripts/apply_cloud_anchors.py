"""cloud_anchors 스키마(10단계)를 기존 DB 에 적용한다 (멱등).

`db/init/*.sql` 은 postgres 컨테이너 최초 기동 시에만 실행된다. 이미 볼륨이 있는 DB 엔
04_cloud_anchors.sql 이 적용되지 않으므로 이 스크립트로 직접 넣는다.
(apply_nav_outline.py 와 같은 방식. 모든 DDL 이 IF NOT EXISTS 라 재실행 안전.)

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/apply_cloud_anchors.py
"""
import os
import pathlib
import sys

from sqlalchemy import text

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import engine  # noqa: E402

SCHEMA_FILE = (
    pathlib.Path(__file__).resolve().parent.parent
    / "db" / "init" / "04_cloud_anchors.sql"
)


def main() -> int:
    if not SCHEMA_FILE.exists():
        print(f"[에러] 스키마 파일을 찾을 수 없다: {SCHEMA_FILE}")
        return 1

    sql = SCHEMA_FILE.read_text(encoding="utf-8")
    try:
        with engine.begin() as conn:
            conn.execute(text(sql))
    except Exception as exc:  # noqa: BLE001
        print(f"[에러] 적용 실패: {type(exc).__name__}: {exc}")
        print("DATABASE_URL 이 맞는지, DB 가 떠 있는지 확인하라.")
        return 1

    print(f"[완료] {SCHEMA_FILE.name} 적용됨 (cloud_anchors).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
