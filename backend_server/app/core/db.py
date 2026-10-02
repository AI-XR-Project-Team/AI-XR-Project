"""SQLAlchemy 엔진 / 세션 / get_db 의존성."""
from sqlalchemy import create_engine
from sqlalchemy.orm import declarative_base, sessionmaker

from app.core.config import settings

engine = create_engine(settings.DATABASE_URL, pool_pre_ping=True)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)

# ORM 모델의 공통 Base. 실제 DDL 은 feature/db-postgres-setup 이 진실 소스이며,
# 여기서는 매핑만 한다(테이블 생성/마이그레이션은 하지 않음).
Base = declarative_base()


def get_db():
    """요청 단위 DB 세션을 제공하고 종료 시 닫는 FastAPI 의존성."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
