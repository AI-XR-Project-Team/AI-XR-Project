"""기존 DB 볼륨에 빠진 컬럼을 기동 시 채워 넣는다(멱등).

`db/init/*.sql` 은 컨테이너 최초 기동에만 돈다. 13-1 에서 cloud_anchors 에 컬럼을 더했는데
ORM 모델이 그 컬럼을 SELECT 하므로, DDL 을 빼먹고 서버를 재시작하면 **앵커 목록 API 가
통째로 500** 이 된다(13-3 네비 앱까지 같이 죽는다). 그래서 기동 때 자동으로 맞춘다.
실패해도 서버 기동은 막지 않고 로그만 남긴다.
"""
import logging
import pathlib

from sqlalchemy import text

from app.core.db import engine

_log = logging.getLogger(__name__)

_SQL_FILE = (
    pathlib.Path(__file__).resolve().parents[2]
    / "db" / "init" / "05_cloud_anchor_asset_offset.sql"
)


def _statements(sql: str) -> list[str]:
    body = "\n".join(ln for ln in sql.splitlines() if not ln.strip().startswith("--"))
    return [s.strip() for s in body.split(";") if s.strip()]


def ensure_asset_offset_columns() -> bool:
    """cloud_anchors 에 asset_* 컬럼이 없으면 추가한다. 성공 여부를 돌려준다."""
    try:
        sql = _SQL_FILE.read_text(encoding="utf-8")
        with engine.begin() as conn:
            for stmt in _statements(sql):
                conn.execute(text(stmt))
        _log.info("schema_patch: cloud_anchors asset_offset 컬럼 확인 완료")
        return True
    except Exception as exc:  # noqa: BLE001 — DB 가 없어도 서버는 떠야 한다
        _log.warning("schema_patch: asset_offset 컬럼 적용 실패 — %s: %s",
                     type(exc).__name__, exc)
        return False
