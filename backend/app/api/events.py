"""SmartPark-IoT backend event ingestion API (M3).

POST /events — ingest a validated event. Idempotent by (device_id, event_id)
per ADR-005: replaying the same event 100× stores exactly one logical row
(NFR-003).
"""
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.orm import Session

from app.config import get_settings
from app.database import get_db
from app.models import Device, Event, OccupancySnapshot
from app.schemas import EventCreate, EventOut

router = APIRouter(prefix="/events", tags=["events"])

settings = get_settings()
CAPACITY = 6  # matches firmware config.hpp CAPACITY; overridden by DB later


def _get_or_create_device(db: Session, device_id: str, facility_id: str) -> Device:
    device = (
        db.query(Device)
        .filter_by(facility_id=facility_id, device_id=device_id)
        .first()
    )
    if device is None:
        device = Device(facility_id=facility_id, device_id=device_id)
        db.add(device)
        db.flush()
    device.is_online = True
    return device


def _recompute_occupancy(db: Session, device_id: str) -> int:
    """Recompute occupancy from confirmed passages for this device."""
    entries = (
        db.query(Event)
        .filter_by(device_id=device_id, type="PASSAGE_CONFIRMED_ENTRY")
        .count()
    )
    exits = (
        db.query(Event)
        .filter_by(device_id=device_id, type="PASSAGE_CONFIRMED_EXIT")
        .count()
    )
    count = max(0, entries - exits)
    return min(count, CAPACITY)


@router.post("/", response_model=EventOut, status_code=status.HTTP_201_CREATED)
def ingest_event(event: EventCreate, db: Session = Depends(get_db)):
    """Ingest one event. Idempotent: duplicate (device_id, event_id) is ignored."""
    # Idempotency check — if already stored, return the existing row (NFR-003)
    existing = (
        db.query(Event)
        .filter_by(device_id=event.device_id, event_id=event.event_id)
        .first()
    )
    if existing:
        return existing

    # Register/update device
    device = _get_or_create_device(db, event.device_id, settings.facility_id)

    # Persist event
    db_event = Event(
        event_id=event.event_id,
        device_id=event.device_id,
        type=event.type,
        sensor_id=event.sensor_id,
        value=event.value,
        at_ms=event.at_ms,
        seq=event.seq,
    )
    db.add(db_event)
    db.flush()

    # Recompute occupancy snapshot
    count = _recompute_occupancy(db, event.device_id)
    snapshot = OccupancySnapshot(
        device_id=event.device_id,
        count=count,
        free_spaces=max(0, CAPACITY - count),
    )
    db.add(snapshot)
    db.commit()
    db.refresh(db_event)
    return db_event


@router.get("/", response_model=list[EventOut])
def list_events(
    device_id: Optional[str] = None,
    limit: int = 100,
    db: Session = Depends(get_db),
):
    """List events, optionally filtered by device."""
    q = db.query(Event)
    if device_id:
        q = q.filter_by(device_id=device_id)
    return q.order_by(Event.received_at.desc()).limit(limit).all()