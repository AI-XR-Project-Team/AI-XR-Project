"""네비 확장 맵(4단계) 스키마를 기존 DB 에 적용한다 (멱등).

`db/init/*.sql` 은 postgres 컨테이너가 **최초 기동될 때만** 실행된다.
이미 볼륨이 만들어진 DB 에는 03_nav_outline.sql 이 적용되지 않으므로
이 스크립트로 직접 넣는다. (apply_chat_schema.py 와 같은 방식)

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/apply_nav_outline.py

03_nav_outline.sql 이 ADD COLUMN IF NOT EXISTS 라 여러 번 실행해도 안전하다.
"""
import os
import pathlib
import sys

from sqlalchemy import text

# `python scripts/apply_nav_outline.py` 로 직접 실행해도 app 패키지를 찾도록 루트 추가.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import engine  # noqa: E402

SCHEMA_FILE = (
    pathlib.Path(__file__).resolve().parent.parent
    / "db" / "init" / "03_nav_outline.sql"
)


def main() -> int:
    if not SCHEMA_FILE.exists():
        print(f"[에러] 스키마 파일을 찾을 수 없다: {SCHEMA_FILE}")
        return 1

    sql = SCHEMA_FILE.read_text(encoding="utf-8")

    try:
        with engine.begin() as conn:
            conn.execute(text(sql))
    except Exception as exc:
        print(f"[에러] 적용 실패: {type(exc).__name__}: {exc}")
        print("DATABASE_URL 이 맞는지, DB 가 떠 있는지 확인하라.")
        return 1

    print(f"[완료] {SCHEMA_FILE.name} 적용됨 (map_spaces.outline_json).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
