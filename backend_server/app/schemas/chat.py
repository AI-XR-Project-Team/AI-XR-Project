import datetime
import uuid
from typing import List, Literal, Optional

from pydantic import BaseModel, ConfigDict, Field


class SessionCreateRequest(BaseModel):
    model_config = ConfigDict(
        json_schema_extra={
            "example": {
                "device_uuid": "android-anonymous-0001",
                "exhibit_id": "22222222-2222-2222-2222-222222222222",
            }
        }
    )

    device_uuid: Optional[str] = Field(
        None, max_length=128, description="모바일 기기 익명 식별자."
    )
    exhibit_id: Optional[uuid.UUID] = Field(
        None,
        description=(
            "이 대화가 다룰 전시물. 부위를 지목하지 않은 질문의 배경이 된다. "
            "생략하면 매 요청마다 `exhibit_id` 나 `poi_id` 를 줘야 한다."
        ),
    )
    exhibit_key: Optional[str] = Field(
        None,
        max_length=200,
        description=(
            "전시물을 UUID 대신 안정 자연키(`dinosaurs.model_asset_key`, 예: "
            "`trex_full_skeleton`)로 지정한다. `exhibit_id` 는 재시드마다 바뀌므로 "
            "클라이언트는 이 키를 쓴다. 둘 다 주면 `exhibit_key` 가 우선한다."
        ),
    )


class SessionCreateResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    session_id: uuid.UUID
    exhibit_id: Optional[uuid.UUID] = None


class ChatAskRequest(BaseModel):
    model_config = ConfigDict(
        json_schema_extra={
            "example": {
                "session_id": "44444444-4444-4444-4444-444444444444",
                "message": "이 공룡은 무엇을 먹었나요?",
                "poi_id": "33333333-3333-3333-3333-333333333333",
            }
        }
    )

    session_id: uuid.UUID = Field(..., description="`POST /docent/sessions` 로 발급받은 세션.")
    message: str = Field(..., min_length=1, max_length=500, description="관람객의 질문.")
    poi_id: Optional[uuid.UUID] = Field(
        None,
        description=(
            "이번 질문에서 관람객이 탭한 부위. 생략하면 전시물 전체를 대상으로 "
            "답한다. 턴마다 달라질 수 있어 세션이 아닌 요청 단위로 받는다."
        ),
    )
    exhibit_id: Optional[uuid.UUID] = Field(
        None,
        description="세션에 전시물이 지정되지 않았을 때 이번 요청에서만 지정한다.",
    )
    exhibit_key: Optional[str] = Field(
        None,
        max_length=200,
        description=(
            "세션에 전시물이 없을 때 이번 요청에서만 안정 자연키"
            "(`dinosaurs.model_asset_key`)로 전시물을 지정한다. `exhibit_id` 보다 우선한다."
        ),
    )


class ChatMessageRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    role: Literal["user", "assistant"]
    content: str
    poi_id: Optional[uuid.UUID] = None
    source: Optional[str] = None
    created_at: Optional[datetime.datetime] = None


class ChatHistoryResponse(BaseModel):
    session_id: uuid.UUID
    exhibit_id: Optional[uuid.UUID] = None
    messages: List[ChatMessageRead] = []


class ChatAskResponse(BaseModel):
    """비스트리밍 응답. SSE 를 쓸 수 없는 환경과 테스트용."""

    model_config = ConfigDict(from_attributes=True)

    session_id: uuid.UUID
    message_id: int
    answer: str
    source: Literal["llm", "fallback", "error"] = Field(
        ...,
        description=(
            "`llm`: 모델 생성 / `fallback`: LLM 실패로 사전 작성 해설 / "
            "`error`: 생성 중 중단되어 부분 응답만 있음"
        ),
    )
    ttft_ms: int = Field(..., description="첫 토큰까지 걸린 시간(ms).")
    total_ms: int = Field(..., description="전체 생성 시간(ms).")
