"""SmartPark-IoT backend Pydantic schemas (M3)."""
from pydantic import BaseModel, ConfigDict
from datetime import datetime
from typing import Optional, List


# ---- Events ----

class EventBase(BaseModel):
    event_id: str
    device_id: str
    type: str
    sensor_id: Optional[int] = None
    value: Optional[int] = None
    at_ms: Optional[int] = None
    seq: Optional[int] = None


class EventCreate(EventBase):
    pass


class EventOut(EventBase):
    id: int
    received_at: datetime

    model_config = ConfigDict(from_attributes=True)


# ---- Devices ----

class DeviceBase(BaseModel):
    facility_id: str
    device_id: str


class DeviceOut(DeviceBase):
    id: int
    first_seen: datetime
    last_seen: datetime
    is_online: bool

    model_config = ConfigDict(from_attributes=True)


# ---- Occupancy ----

class OccupancyOut(BaseModel):
    device_id: str
    count: int
    free_spaces: int
    recorded_at: datetime


# ---- Auth ----

class Token(BaseModel):
    access_token: str
    token_type: str


class TokenData(BaseModel):
    username: Optional[str] = None


# ---- Capacity config ----

class CapacityUpdate(BaseModel):
    capacity: int