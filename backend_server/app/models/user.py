from sqlalchemy import Column, DateTime, String, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class User(Base):
    __tablename__ = "users"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    device_uuid = Column(String(128), unique=True, nullable=False)
    created_at = Column(DateTime(timezone=True), server_default=text("now()"))
