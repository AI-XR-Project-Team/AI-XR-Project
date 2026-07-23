import datetime
import uuid
from decimal import Decimal
from typing import Optional

from pydantic import BaseModel, ConfigDict


class DinosaurRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    name_ko: str
    name_sci: str
    period: Optional[str] = None
    length_m: Optional[Decimal] = None
    model_asset_key: Optional[str] = None
    ai_prompt_context: Optional[str] = None
    created_at: Optional[datetime.datetime] = None
