import time

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.schemas.docent import DocentAskRequest, DocentAskResponse
from app.services.docent import ask_docent, load_poi_context
from app.services.llm.base import LlmClient
from app.services.llm.factory import get_llm_client

router = APIRouter()


@router.post(
    "/docent/ask",
    response_model=DocentAskResponse,
    summary="POI 기반 AI 도슨트 질의",
    responses={404: {"description": "해당 POI 가 존재하지 않음"}},
)
def ask(
    payload: DocentAskRequest,
    db: Session = Depends(get_db),
    llm: LlmClient = Depends(get_llm_client),
):
    """관람객이 응시한 POI 에 대한 AI 도슨트 응답을 반환한다.

    POI → 전시물 → 공룡 순으로 DB 컨텍스트를 모아 프롬프트를 조립한 뒤 LLM 에 묻는다.
    LLM 이 실패하거나 빈 응답을 주면 `pois.docent_text` 를 `source="fallback"` 으로
    돌려주므로, 이 엔드포인트는 LLM 장애 시에도 500 을 내지 않는다.

    `question` 을 생략하면 해당 부위의 기본 해설을 생성한다.
    """
    started = time.perf_counter()

    context = load_poi_context(db, payload.poi_id)
    if context is None:
        raise HTTPException(status_code=404, detail="poi not found")
    poi, exhibit, dinosaur = context

    result = ask_docent(poi, exhibit, dinosaur, payload.question, llm)

    return DocentAskResponse(
        poi_id=poi.id,
        exhibit_id=exhibit.id,
        answer=result.answer,
        source=result.source,
        elapsed_ms=int((time.perf_counter() - started) * 1000),
    )
