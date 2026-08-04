"""챗봇 대화 스키마를 기존 DB 에 적용한다 (멱등).

`db/init/*.sql` 은 postgres 컨테이너가 **최초 기동될 때만** 실행된다.
이미 볼륨이 만들어진 DB 에는 02_chat_schema.sql 이 적용되지 않으므로
이 스크립트로 직접 넣는다.

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/apply_chat_schema.py

02_chat_schema.sql 이 전부 IF NOT EXISTS 라서 여러 번 실행해도 안전하다.
볼륨을 새로 만든 경우(docker compose down -v) 에는 이미 적용돼 있으므로
실행할 필요가 없지만, 실행해도 아무 일도 일어나지 않는다.
"""
import pathlib
import sys

from sqlalchemy import text

from app.core.db import engine

SCHEMA_FILE = pathlib.Path(__file__).resolve().parent.parent / "db" / "init" / "02_chat_schema.sql"


def main() -> int:
    if not SCHEMA_FILE.exists():
        print(f"[에러] 스키마 파일을 찾을 수 없다: {SCHEMA_FILE}")
        return 1

    sql = SCHEMA_FILE.read_text(encoding="utf-8")

    try:
        with engine.begin() as conn:
            # DDL 여러 개를 한 번에 보낸다. psycopg2 는 세미콜론으로 구분된
            # 문장을 그대로 실행할 수 있고, engine.begin() 이 트랜잭션을
            # 감싸므로 중간에 실패하면 전부 롤백된다.
            conn.execute(text(sql))
    except Exception as exc:
        print(f"[에러] 적용 실패: {type(exc).__name__}: {exc}")
        print("DATABASE_URL 이 맞는지, DB 가 떠 있는지 확인하라.")
        return 1

    print(f"[완료] {SCHEMA_FILE.name} 적용됨 (chat_sessions, chat_messages).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
