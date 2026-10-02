import datetime
import uuid
from decimal import Decimal
from typing import List, Optional

from pydantic import BaseModel, ConfigDict


class PoiRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    part_name: str
    pos_x_cm: Decimal  # UE5 Z-up, cm
    pos_y_cm: Decimal
    pos_z_cm: Decimal
    docent_text: Optional[str] = None


class ExhibitRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    dinosaur_id: uuid.UUID
    label: str
    anchor_hint: Optional[str] = None
    created_at: Optional[datetime.datetime] = None
    pois: List[PoiRead] = []
