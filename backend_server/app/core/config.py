"""환경설정 — .env 의 DATABASE_URL 등을 로드한다."""
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # feature/db-postgres-setup 의 .env.example 과 동일한 기본값.
    # .env 가 있으면 그 값으로 덮어쓴다.
    DATABASE_URL: str = "postgresql+psycopg2://dino:dino_dev_pw@localhost:5432/dino_ar"

    # --- AI 도슨트 LLM (feature/ai-docent-service) ---
    # 지원 값은 app/services/llm/factory.py 의 _PROVIDERS 참고.
    # 기본값은 API 키 없이 동작하는 mock. 실제 사용 시 .env 에서 gemini 로 바꾼다.
    LLM_PROVIDER: str = "mock"
    # 실제 키는 .env 에만 둔다(.gitignore 대상). 코드나 .env.example 에 넣지 말 것.
    LLM_API_KEY: str = ""
    # Google AI Studio 모델. 실측: 3.6-flash + MINIMAL = 약 1.5초 (2초 KPI 충족).
    # gemini-2.5-flash 는 신규 사용자에게 더 이상 제공되지 않는다(404).
    LLM_MODEL: str = "gemini-3.6-flash"
    # Gemini API 는 10초 미만 deadline 을 거부한다. KPI 가 아니라 '멎었을 때 상한'.
    LLM_TIMEOUT_SEC: float = 10.0
    # 응답 텍스트 상한. 3~4문장 해설 기준 넉넉한 값.
    LLM_MAX_TOKENS: int = 2048
    # minimal | low | medium | high — Gemini 3.x 의 사고 깊이.
    # 실측상 low 만 돼도 4초대로 뛰므로 도슨트는 minimal 고정.
    LLM_THINKING_LEVEL: str = "minimal"

    # --- 챗봇 대화 (feature/mobile-docent-chat) ---
    # 프롬프트에 실어 보낼 최근 메시지 개수(질문 1건 = 1, 답변 1건 = 1).
    # 8 이면 주고받은 대화 4쌍이다. 전체 이력을 매번 넣으면 첫 토큰까지의
    # 지연과 토큰 비용이 함께 늘어난다.
    CHAT_HISTORY_TURNS: int = 8


settings = Settings()
