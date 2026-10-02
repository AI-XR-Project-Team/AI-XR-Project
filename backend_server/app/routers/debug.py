"""임시 디버그 로그 수신 — 앱이 CSV 조각을 붙여 보낸다(측위 프로브·현장 로그 공용).

검증이 끝나면 파일째 삭제한다. 스키마는 앱이 정한다(헤더 줄을 앱이 첫 조각으로 보낸다)
→ 서버는 헤더를 주입하지 않고 body 를 그대로 append 한다.
"""
from datetime import datetime, timezone
from pathlib import Path

from fastapi import APIRouter, Request

router = APIRouter(prefix="/debug", tags=["debug"])

# backend_server/app/routers/debug.py → parents[3] = 프로젝트 루트
LOG_DIR = Path(__file__).resolve().parents[3] / "docs" / "probe-logs"


@router.post("/probe-log")
async def probe_log(request: Request, session: str = "unnamed"):
    """세션 태그별로 한 파일에 append. body 는 앱이 만든 CSV(헤더 포함)를 그대로 쓴다."""
    body = await request.body()
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    safe = "".join(c for c in session if c.isalnum() or c in "-_")[:40] or "unnamed"
    path = LOG_DIR / f"{safe}.csv"
    with path.open("ab") as f:
        f.write(body)
    return {"ok": True, "file": path.name, "bytes": len(body),
            "at": datetime.now(timezone.utc).isoformat()}
