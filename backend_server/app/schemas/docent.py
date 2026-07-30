import uuid
from typing import Literal, Optional

from pydantic import BaseModel, ConfigDict, Field


class DocentAskRequest(BaseModel):
    model_config = ConfigDict(
        json_schema_extra={
            "example": {
                "poi_id": "00000000-0000-0000-0000-000000000000",
                "question": "이 공룡은 무엇을 먹었나요?",
                "device_uuid": "quest3-anonymous-0001",
            }
        }
    )

    poi_id: uuid.UUID = Field(
        ...,
        description="관람객이 응시(Gaze)한 POI 의 id. `GET /exhibits/{id}` 응답에서 얻는다.",
    )
    question: Optional[str] = Field(
        None,
        max_length=500,
        description="관람객의 자유 질문. 생략하면 해당 부위의 기본 해설을 생성한다.",
    )
    device_uuid: Optional[str] = Field(
        None,
        max_length=128,
        description=(
            "Quest 3 익명 기기 식별자. 향후 Q&A 로그 적재용으로 미리 받아두며 "
            "현재는 사용하지 않는다."
        ),
    )


class DocentAskResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    poi_id: uuid.UUID
    exhibit_id: uuid.UUID
    answer: str = Field(..., description="도슨트 응답 본문")
    source: Literal["llm", "fallback"] = Field(
        ...,
        description=(
            "`llm`: 모델이 생성한 응답 / "
            "`fallback`: LLM 실패로 DB 의 사전 작성 해설(`pois.docent_text`)을 반환"
        ),
    )
    elapsed_ms: int = Field(
        ..., description="서버 처리 시간(ms). 도슨트 응답 <=2s KPI 계측용."
    )
