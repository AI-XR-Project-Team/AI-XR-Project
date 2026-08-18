"""6단계 임시 — 마커 프로브 로그 수신. 6단계 종료 시 파일째 삭제한다."""
from datetime import datetime, timezone
from pathlib import Path

from fastapi import APIRouter, Request

router = APIRouter(prefix="/debug", tags=["debug"])

# backend_server/app/routers/debug.py → parents[3] = 프로젝트 루트
LOG_DIR = Path(__file__).resolve().parents[3] / "docs" / "probe-logs"
HEADER = ("t_sec,event,marker,dist_cm,ang_x_deg,ang_y_deg,ang_z_deg,"
          "state,simultaneous,quality,quality_reason\n")


@router.post("/probe-log")
async def probe_log(request: Request, session: str = "unnamed"):
    """앱이 CSV 조각을 text/plain 으로 붙여 보낸다. 세션 태그별로 한 파일에 append."""
    body = await request.body()
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    safe = "".join(c for c in session if c.isalnum() or c in "-_")[:40] or "unnamed"
    path = LOG_DIR / f"probe_{safe}.csv"
    new = not path.exists()
    with path.open("ab") as f:
        if new:
            f.write(HEADER.encode())
        f.write(body)
    return {"ok": True, "file": path.name, "bytes": len(body),
            "at": datetime.now(timezone.utc).isoformat()}
