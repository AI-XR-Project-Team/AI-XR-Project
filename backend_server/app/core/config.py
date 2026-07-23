"""환경설정 — .env 의 DATABASE_URL 등을 로드한다."""
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # feature/db-postgres-setup 의 .env.example 과 동일한 기본값.
    # .env 가 있으면 그 값으로 덮어쓴다.
    DATABASE_URL: str = "postgresql+psycopg2://dino:dino_dev_pw@localhost:5432/dino_ar"


settings = Settings()
