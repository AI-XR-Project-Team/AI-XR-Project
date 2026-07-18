from fastapi import APIRouter

router = APIRouter()


@router.get("/health")
def health():
    """서버 생존 확인."""
    return {"status": "ok"}
