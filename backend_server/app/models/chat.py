"""챗봇 대화 세션 / 메시지 ORM 매핑.

기존 도슨트(`POST /docent/ask`)는 단발 질의응답이라 상태가 없었다. 모바일
챗봇은 "그럼 걔는?" 같은 후속 질문이 통해야 하므로 대화 이력을 남긴다.
`feature-ai-docent-service` §7 에서 미뤄둔 Q&A 로그 적재를 겸한다.

기존 모델(dinosaur/exhibit/poi)은 `server_default=text("gen_random_uuid()")`
처럼 PostgreSQL 전용 기본값을 쓰지만, 여기서는 **파이썬 측 기본값**을 쓴다.
그래야 SQLite 인메모리로 대화 흐름 전체를 테스트할 수 있다(이 개발 환경에는
Docker 가 없어 PostgreSQL 을 띄울 수 없다). 실제 DDL 은
`db/init/02_chat_schema.sql` 이 진실 소스이며 거기서는 서버 기본값을 쓴다.
"""
import uuid
from datetime import datetime, timezone

from sqlalchemy import (
    BigInteger,
    Column,
    DateTime,
    ForeignKey,
    Integer,
    String,
    Text,
)
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import relationship

from app.core.db import Base

# 대화 참여자 역할. LLM 벤더별 표기(Gemini 는 "model")로의 변환은
# 어댑터가 담당하고, 저장 계층은 이 두 값만 쓴다.
ROLE_USER = "user"
ROLE_ASSISTANT = "assistant"


def _utcnow() -> datetime:
    """DB 서버 시각 대신 앱에서 UTC 를 채운다(SQLite 호환 목적)."""
    return datetime.now(timezone.utc)


class ChatSession(Base):
    """관람객 1명의 대화 1건.

    로그인이 없으므로 `device_uuid` 로만 느슨하게 묶는다. `exhibit_id` 는
    "이 대화가 어느 전시물에 대한 것인지"로, POI 를 고르지 않은 자유대화에서
    배경 컨텍스트를 결정하는 데 쓰인다.
    """

    __tablename__ = "chat_sessions"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    device_uuid = Column(String(128))
    exhibit_id = Column(
        UUID(as_uuid=True), ForeignKey("exhibits.id", ondelete="SET NULL")
    )
    created_at = Column(DateTime(timezone=True), nullable=False, default=_utcnow)
    last_active_at = Column(DateTime(timezone=True), nullable=False, default=_utcnow)

    # id 순 = 삽입 순. created_at 은 같은 초에 여러 건이 들어오면 순서가
    # 흔들리므로 정렬 기준으로 쓰지 않는다.
    messages = relationship(
        "ChatMessage",
        back_populates="session",
        order_by="ChatMessage.id",
        cascade="all, delete-orphan",
    )


class ChatMessage(Base):
    """대화 한 턴. user 질문과 assistant 응답이 각각 한 행이다."""

    __tablename__ = "chat_messages"

    # BIGINT 는 SQLite 에서 자동증가하지 않는다(INTEGER PRIMARY KEY 만 된다).
    # 운영 DB 는 BIGSERIAL 을 쓰되 테스트에서도 돌도록 variant 를 준다.
    id = Column(
        BigInteger().with_variant(Integer, "sqlite"),
        primary_key=True,
        autoincrement=True,
    )
    session_id = Column(
        UUID(as_uuid=True),
        ForeignKey("chat_sessions.id", ondelete="CASCADE"),
        nullable=False,
    )
    role = Column(String(16), nullable=False)  # ROLE_USER | ROLE_ASSISTANT
    content = Column(Text, nullable=False)

    # 이 턴에서 관람객이 지목한 부위. 자유대화(부위 미선택)면 NULL 이다.
    poi_id = Column(UUID(as_uuid=True), ForeignKey("pois.id"))

    # 아래는 assistant 행에만 채워지는 진단 정보.
    source = Column(String(16))  # "llm" | "fallback"
    # 스트리밍에서는 체감 지표가 총 소요시간이 아니라 첫 토큰까지의 시간이다.
    ttft_ms = Column(Integer)
    total_ms = Column(Integer)

    created_at = Column(DateTime(timezone=True), nullable=False, default=_utcnow)

    session = relationship("ChatSession", back_populates="messages")
