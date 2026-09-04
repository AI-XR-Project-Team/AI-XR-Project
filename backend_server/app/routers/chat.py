"""모바일 챗봇 대화 엔드포인트.

`POST /docent/ask` 는 단발 질의라 그대로 두고(AR 게이즈 경로가 쓴다), 대화가
이어지는 챗봇은 여기서 따로 다룬다.

SSE 를 쓰는 이유는 첫 토큰까지의 시간(~1.5초)만으로 화면에 글자가 흐르기
시작해 체감 대기가 크게 줄기 때문이다. UE5 클라이언트가 이 프레임 규약에
맞춰 파서를 구현한다.
"""
import json
import logging
import uuid
from typing import List, Optional, Tuple

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import StreamingResponse
from sqlalchemy.orm import Session
from starlette.concurrency import iterate_in_threadpool, run_in_threadpool

from app.core.config import settings
from app.core.db import SessionLocal, get_db
from app.models.chat import ROLE_USER, ChatMessage, ChatSession
from app.models.poi import Poi
from app.schemas.chat import (
    ChatAskRequest,
    ChatAskResponse,
    ChatHistoryResponse,
    SessionCreateRequest,
    SessionCreateResponse,
)
from app.services.chat import (
    ChatDelta,
    ChatDone,
    ChatError,
    build_history,
    make_assistant_message,
    stream_chat,
)
from app.services.docent import (
    build_chat_fallback,
    build_chat_prompt,
    build_general_chat_fallback,
    build_general_chat_prompt,
    find_exhibit_by_key,
    load_exhibit_context,
    load_museum_roster,
)
from app.services.llm.base import ChatTurn, LlmClient
from app.services.llm.factory import get_llm_client

logger = logging.getLogger(__name__)

router = APIRouter()


def _sse(event: str, payload: dict) -> str:
    """SSE 프레임 한 개를 만든다.

    data 는 반드시 JSON 으로 감싼다. 응답 본문에 들어 있는 개행이 그대로 나가면
    프레임이 중간에 끊겨 클라이언트 파서가 깨진다. json.dumps 가 개행을 \\n 으로
    이스케이프하므로 한 줄이 보장된다.
    """
    return f"event: {event}\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"


@router.post(
    "/docent/sessions",
    response_model=SessionCreateResponse,
    summary="챗봇 대화 세션 생성",
    tags=["chat"],
)
def create_session(payload: SessionCreateRequest, db: Session = Depends(get_db)):
    """대화 세션을 만든다. 이후 모든 대화 요청은 이 `session_id` 를 쓴다.

    전시물은 안정 자연키(`exhibit_key`)로 받는 것이 정석이다. `exhibit_id`(UUID)는
    재시드마다 바뀌어 클라이언트가 들고 있으면 깨지므로 하위호환용으로만 남긴다.
    """
    exhibit_id = payload.exhibit_id
    if payload.exhibit_key is not None:
        exhibit = find_exhibit_by_key(db, payload.exhibit_key)
        if exhibit is None:
            raise HTTPException(status_code=404, detail="exhibit not found")
        exhibit_id = exhibit.id
    elif exhibit_id is not None and load_exhibit_context(db, exhibit_id) is None:
        raise HTTPException(status_code=404, detail="exhibit not found")

    session = ChatSession(device_uuid=payload.device_uuid, exhibit_id=exhibit_id)
    db.add(session)
    db.commit()
    db.refresh(session)

    return SessionCreateResponse(session_id=session.id, exhibit_id=session.exhibit_id)


@router.get(
    "/docent/sessions/{session_id}/messages",
    response_model=ChatHistoryResponse,
    summary="대화 이력 조회",
    tags=["chat"],
    responses={404: {"description": "세션이 존재하지 않음"}},
)
def get_history(session_id: uuid.UUID, db: Session = Depends(get_db)):
    """앱을 다시 켰을 때 이전 대화를 복원하는 용도."""
    session = db.get(ChatSession, session_id)
    if session is None:
        raise HTTPException(status_code=404, detail="session not found")

    return ChatHistoryResponse(
        session_id=session.id,
        exhibit_id=session.exhibit_id,
        messages=session.messages,
    )


def _prepare_turn(
    db: Session, payload: ChatAskRequest
) -> Tuple[ChatSession, str, List[ChatTurn], str, Optional[uuid.UUID]]:
    """대화 한 턴에 필요한 것을 DB 에서 모두 읽어 온다.

    스트리밍이 시작된 뒤에는 DB 를 만지지 않는다. 필요한 값을 여기서 전부
    확정해 두고, 이후에는 순수 계산과 LLM 호출만 남긴다.

    사용자 질문도 여기서 저장한다. 클라이언트가 도중에 끊더라도 질문은 남아야
    다음 대화의 맥락이 이어진다.
    """
    session = db.get(ChatSession, payload.session_id)
    if session is None:
        raise HTTPException(status_code=404, detail="session not found")

    poi: Optional[Poi] = None
    exhibit_id = payload.exhibit_id or session.exhibit_id

    # 안정 자연키가 오면 UUID 보다 우선한다. 세션에 전시물이 안 붙은 채로
    # 대화가 시작돼도 이 요청의 키로 맥락을 살린다.
    if payload.exhibit_key is not None:
        keyed = find_exhibit_by_key(db, payload.exhibit_key)
        if keyed is None:
            raise HTTPException(status_code=404, detail="exhibit not found")
        exhibit_id = keyed.id

    if payload.poi_id is not None:
        poi = db.get(Poi, payload.poi_id)
        if poi is None:
            raise HTTPException(status_code=404, detail="poi not found")
        # 부위가 지정되면 그 부위가 속한 전시물이 대화의 대상이다.
        exhibit_id = poi.exhibit_id

    # 관람객은 "비슷한 공룡 있어?" 처럼 이 전시물 밖을 묻는다. 실제 전시물
    # 목록을 함께 넘겨야 모델이 없는 전시관을 지어내지 않는다.
    roster = load_museum_roster(db)

    if exhibit_id is None:
        # 마커로 전시물을 고르기 전에도 대화는 되어야 한다. 클라이언트가 전시물
        # 없이 세션을 열고 묻는 경로(일반 AI 챗)를 여기서 받는다. 전시물 맥락
        # 없이 도슨트 페르소나만으로 답하고, 세부 정보가 필요하면 마커 인식을
        # 안내하게 한다.
        system_prompt = build_general_chat_prompt(roster)
        fallback_text = build_general_chat_fallback()
    else:
        context = load_exhibit_context(db, exhibit_id)
        if context is None:
            raise HTTPException(status_code=404, detail="exhibit not found")
        exhibit, dinosaur, pois = context

        system_prompt = build_chat_prompt(exhibit, dinosaur, pois, poi, roster)
        fallback_text = build_chat_fallback(poi, exhibit, dinosaur, pois)

    history = build_history(session.messages, settings.CHAT_HISTORY_TURNS)

    db.add(
        ChatMessage(
            session_id=session.id,
            role=ROLE_USER,
            content=payload.message,
            poi_id=poi.id if poi is not None else None,
        )
    )
    db.commit()

    return session, system_prompt, history, fallback_text, (poi.id if poi else None)


def _persist_assistant(session_id: uuid.UUID, poi_id, event) -> int:
    """생성이 끝난 뒤 응답을 저장하고 메시지 id 를 돌려준다.

    요청에 주입된 세션을 재사용하지 않고 새로 연다. 스트리밍 응답과 `yield`
    의존성의 정리 시점 관계는 FastAPI 버전에 따라 바뀐 이력이 있고,
    requirements.txt 가 버전을 고정하지 않는다. 여기서 세션을 직접 관리하면
    그 변화에 영향받지 않는다.
    """
    db = SessionLocal()
    try:
        message = make_assistant_message(session_id, poi_id, event)
        db.add(message)

        session = db.get(ChatSession, session_id)
        if session is not None:
            from app.models.chat import _utcnow

            session.last_active_at = _utcnow()

        db.commit()
        return message.id
    finally:
        db.close()


@router.post(
    "/docent/chat",
    response_model=ChatAskResponse,
    summary="챗봇 대화 (비스트리밍)",
    tags=["chat"],
    responses={
        404: {"description": "세션 / POI / 전시물이 존재하지 않음"},
    },
)
def chat(
    payload: ChatAskRequest,
    db: Session = Depends(get_db),
    llm: LlmClient = Depends(get_llm_client),
):
    """응답 전체를 한 번에 반환한다.

    SSE 를 쓸 수 없는 환경과 테스트용이며, 대화 로직은 스트리밍 경로와 동일하다.
    """
    session, system_prompt, history, fallback_text, poi_id = _prepare_turn(db, payload)

    answer_parts: List[str] = []
    final = None
    for event in stream_chat(llm, system_prompt, history, payload.message, fallback_text):
        if isinstance(event, ChatDelta):
            answer_parts.append(event.text)
        else:
            final = event

    message_id = _persist_assistant(session.id, poi_id, final)

    if isinstance(final, ChatError):
        return ChatAskResponse(
            session_id=session.id,
            message_id=message_id,
            answer=final.full_text,
            source="error",
            ttft_ms=0,
            total_ms=0,
        )

    return ChatAskResponse(
        session_id=session.id,
        message_id=message_id,
        answer=final.full_text,
        source=final.source,
        ttft_ms=final.ttft_ms,
        total_ms=final.total_ms,
    )


@router.post(
    "/docent/chat/stream",
    summary="챗봇 대화 (SSE 스트리밍)",
    tags=["chat"],
    response_class=StreamingResponse,
    responses={
        200: {
            "content": {"text/event-stream": {}},
            "description": (
                "SSE 스트림. 프레임 종류:\n"
                "- `meta`  : `{session_id, poi_id}` — 생성 시작\n"
                "- `delta` : `{text}` — 응답 조각. 이어 붙이면 전체 응답\n"
                "- `done`  : `{message_id, source, ttft_ms, total_ms}` — 정상 종료\n"
                "- `error` : `{detail, partial}` — 실패. `partial=true` 면 "
                "앞서 받은 `delta` 는 유효하다"
            ),
        },
        404: {"description": "세션 / POI / 전시물이 존재하지 않음"},
    },
)
async def chat_stream(
    payload: ChatAskRequest,
    db: Session = Depends(get_db),
    llm: LlmClient = Depends(get_llm_client),
):
    """응답을 생성되는 대로 흘려보낸다.

    동기 SQLAlchemy 와 동기 LLM 호출을 async 라우터에서 그대로 부르면 이벤트
    루프가 막힌다. DB 준비는 run_in_threadpool, LLM 스트림은
    iterate_in_threadpool 로 각각 워커 스레드에 넘긴다.
    """
    session, system_prompt, history, fallback_text, poi_id = await run_in_threadpool(
        _prepare_turn, db, payload
    )

    async def event_stream():
        yield _sse(
            "meta",
            {"session_id": str(session.id), "poi_id": str(poi_id) if poi_id else None},
        )

        final = None
        events = stream_chat(llm, system_prompt, history, payload.message, fallback_text)

        async for event in iterate_in_threadpool(events):
            if isinstance(event, ChatDelta):
                yield _sse("delta", {"text": event.text})
            else:
                final = event

        message_id = await run_in_threadpool(_persist_assistant, session.id, poi_id, final)

        if isinstance(final, ChatError):
            yield _sse(
                "error",
                {"detail": final.detail, "partial": final.partial, "message_id": message_id},
            )
        else:
            yield _sse(
                "done",
                {
                    "message_id": message_id,
                    "source": final.source,
                    "ttft_ms": final.ttft_ms,
                    "total_ms": final.total_ms,
                },
            )

    return StreamingResponse(
        event_stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            # 리버스 프록시(nginx)가 응답을 버퍼링하면 스트리밍이 무의미해진다.
            "X-Accel-Buffering": "no",
        },
    )
