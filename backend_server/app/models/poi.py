from sqlalchemy import Column, ForeignKey, Numeric, String, Text, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class Poi(Base):
    __tablename__ = "pois"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    exhibit_id = Column(UUID(as_uuid=True),
                        ForeignKey("exhibits.id", ondelete="CASCADE"),
                        nullable=False)
    part_name = Column(String(100), nullable=False)
    # 좌표: UE5 Z-up, cm
    pos_x_cm = Column(Numeric(8, 2), nullable=False)
    pos_y_cm = Column(Numeric(8, 2), nullable=False)
    pos_z_cm = Column(Numeric(8, 2), nullable=False)
    docent_text = Column(Text)
