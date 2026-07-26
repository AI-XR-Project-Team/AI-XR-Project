"""환경설정 — .env 의 DATABASE_URL 등을 로드한다."""
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # feature/db-postgres-setup 의 .env.example 과 동일한 기본값.
    # .env 가 있으면 그 값으로 덮어쓴다.
    DATABASE_URL: str = "postgresql+psycopg2://dino:dino_dev_pw@localhost:5432/dino_ar"

    # --- AI 도슨트 LLM (feature/ai-docent-service) ---
    # 벤더 미정 상태이므로 기본값은 API 키 없이 동작하는 mock 이다.
    # 지원 값은 app/services/llm/factory.py 의 _PROVIDERS 참고.
    LLM_PROVIDER: str = "mock"
    LLM_API_KEY: str = ""
    LLM_MODEL: str = ""
    LLM_TIMEOUT_SEC: float = 4.0


settings = Settings()
