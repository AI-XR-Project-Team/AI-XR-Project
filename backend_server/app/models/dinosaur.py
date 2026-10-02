from sqlalchemy import Column, DateTime, Numeric, String, Text, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class Dinosaur(Base):
    __tablename__ = "dinosaurs"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    name_ko = Column(String(100), nullable=False)
    name_sci = Column(String(100), nullable=False)
    period = Column(String(50))
    length_m = Column(Numeric(5, 2))
    model_asset_key = Column(String(200))
    ai_prompt_context = Column(Text)
    created_at = Column(DateTime(timezone=True), server_default=text("now()"))
