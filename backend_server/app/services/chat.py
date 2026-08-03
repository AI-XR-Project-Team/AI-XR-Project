"""챗봇 대화 오케스트레이션 — 히스토리 조립 → 스트리밍 생성 → 폴백.

이 모듈은 HTTP 를 모른다. 이벤트 객체를 순서대로 내보내고, SSE 프레이밍은
라우터가 담당한다. 덕분에 대화 로직을 HTTP 없이 테스트할 수 있다.

폴백 규약 (기존 단발 API 와 달라지는 지점):
    기존 `POST /docent/ask` 는 "LLM 이 죽어도 500 대신 docent_text" 를 보장했다.
    스트리밍에서는 첫 조각을 이미 보낸 뒤에도 실패할 수 있어 그 보장이 그대로는
    성립하지 않는다. 그래서 실패 시점으로 나눈다.

      - 첫 조각 이전 실패  → 폴백 문구를 정상 응답처럼 흘려보낸다.
                             (source="fallback". 기존 보장이 그대로 유지된다.)
      - 첫 조각 이후 실패  → 받은 만큼은 살리고 오류를 알린다.
                             (ChatError(partial=True). 이미 보낸 텍스트는 회수 불가.)

    어느 쪽이든 그때까지 생성된 텍스트는 호출부가 저장할 수 있도록 이벤트에 싣는다.
"""
import logging
import time
from dataclasses import dataclass
from typing import Iterator, List, Optional, Sequence, Union

from app.models.chat import ROLE_ASSISTANT, ChatMessage
from app.services.llm.base import ChatTurn, LlmClient, LlmError

logger = logging.getLogger(__name__)


@dataclass
class ChatDelta:
    """응답 조각 하나."""

    text: str


@dataclass
class ChatDone:
    """정상 종료. `full_text` 는 저장·검증용 누적 결과다."""

    full_text: str
    source: str  # "llm" | "fallback"
    ttft_ms: int
    total_ms: int


@dataclass
class ChatError:
    """생성 실패.

    `partial=True` 면 이미 일부 텍스트가 클라이언트로 나간 상태다.
    """

    detail: str
    partial: bool
    full_text: str


ChatEvent = Union[ChatDelta, ChatDone, ChatError]


def build_history(
    messages: Sequence[ChatMessage], max_turns: int
) -> List[ChatTurn]:
    """저장된 메시지에서 최근 대화만 골라 LLM 입력용으로 바꾼다.

    전체 이력을 매번 넣으면 프롬프트가 길어져 첫 토큰까지의 지연이 나빠지고
    토큰 비용도 늘어난다. 최근 `max_turns` 개만 쓴다.

    내용이 빈 메시지는 건너뛴다. 스트리밍이 첫 조각 전에 끊기면 빈 assistant
    행이 남을 수 있는데, 그대로 넣으면 이후 대화 품질이 나빠진다.
    """
    turns = [
        ChatTurn(role=m.role, content=m.content)
        for m in messages
        if m.content and m.content.strip()
    ]
    if max_turns > 0:
        turns = turns[-max_turns:]
    return turns


def stream_chat(
    llm: LlmClient,
    system_prompt: str,
    history: Sequence[ChatTurn],
    question: str,
    fallback_text: str,
) -> Iterator[ChatEvent]:
    """LLM 응답을 조각 단위로 내보내고, 실패하면 위 폴백 규약을 따른다."""
    started = time.perf_counter()
    ttft_ms: Optional[int] = None
    parts: List[str] = []

    def elapsed_ms() -> int:
        return int((time.perf_counter() - started) * 1000)

    try:
        for chunk in llm.stream(system_prompt, history, question):
            if not chunk:
                continue
            if ttft_ms is None:
                # 스트리밍의 체감 지표는 총 소요시간이 아니라 여기다.
                ttft_ms = elapsed_ms()
            parts.append(chunk)
            yield ChatDelta(chunk)
    except LlmError as exc:
        yield from _handle_stream_failure(exc, parts, fallback_text, elapsed_ms)
        return
    except Exception as exc:  # 어댑터가 LlmError 로 감싸지 못한 예외까지 흡수
        logger.exception("대화 생성 중 예상치 못한 예외")
        yield from _handle_stream_failure(exc, parts, fallback_text, elapsed_ms)
        return

    full_text = "".join(parts).strip()
    if not full_text:
        # 예외는 없었지만 아무것도 못 받은 경우. 빈 말풍선보다 폴백이 낫다.
        logger.warning("LLM 이 빈 응답을 반환했다 — 폴백")
        yield ChatDelta(fallback_text)
        yield ChatDone(fallback_text, "fallback", elapsed_ms(), elapsed_ms())
        return

    yield ChatDone(full_text, "llm", ttft_ms or elapsed_ms(), elapsed_ms())


def _handle_stream_failure(
    exc: BaseException,
    parts: List[str],
    fallback_text: str,
    elapsed_ms,
) -> Iterator[ChatEvent]:
    """실패 시점에 따라 폴백과 오류 중 하나를 고른다."""
    received = "".join(parts)

    if received:
        # 이미 나간 텍스트는 회수할 수 없다. 폴백으로 덮어쓰면 관람객 화면에
        # 두 개의 서로 다른 답변이 이어 붙는다. 받은 만큼만 남기고 알린다.
        logger.warning("스트리밍 중단(부분 응답 %d자): %s", len(received), exc)
        yield ChatError(str(exc), partial=True, full_text=received.strip())
        return

    logger.warning("스트리밍 실패(조각 수신 전) — 폴백: %s", exc)
    yield ChatDelta(fallback_text)
    yield ChatDone(fallback_text, "fallback", elapsed_ms(), elapsed_ms())


def make_assistant_message(
    session_id, poi_id, event: Union[ChatDone, ChatError]
) -> ChatMessage:
    """종료 이벤트를 저장용 ChatMessage 로 바꾼다.

    부분 응답도 저장한다. 다음 턴에서 "아까 하던 얘기" 의 맥락이 되고,
    어디서 끊겼는지 추적할 수 있다.
    """
    if isinstance(event, ChatDone):
        return ChatMessage(
            session_id=session_id,
            role=ROLE_ASSISTANT,
            content=event.full_text,
            poi_id=poi_id,
            source=event.source,
            ttft_ms=event.ttft_ms,
            total_ms=event.total_ms,
        )

    return ChatMessage(
        session_id=session_id,
        role=ROLE_ASSISTANT,
        content=event.full_text,
        poi_id=poi_id,
        source="error",
    )
