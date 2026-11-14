"""SmartPark-IoT backend SQLAlchemy models (M3).

Tables:
  - devices: registered ESP32 devices (facility_id, device_id, last_seen)
  - events: ingested MQTT events, idempotent by (device_id, event_id)
  - occupancy_snapshots: recomputed occupancy per device per event
"""
import sqlalchemy as sa
from sqlalchemy import ForeignKey
from sqlalchemy.orm import Mapped, mapped_column, relationship
from sqlalchemy.sql import func

from app.database import Base


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(primary_key=True, autoincrement=True)
    facility_id: Mapped[str] = mapped_column(sa.String(64), index=True, nullable=False)
    device_id: Mapped[str] = mapped_column(sa.String(64), index=True, nullable=False)
    first_seen: Mapped[str] = mapped_column(
        sa.DateTime(timezone=True), server_default=func.now(), nullable=False
    )
    last_seen: Mapped[str] = mapped_column(
        sa.DateTime(timezone=True), server_default=func.now(), onupdate=func.now(),
        nullable=False,
    )
    is_online: Mapped[bool] = mapped_column(sa.Boolean, default=False, nullable=False)

    # Unique device within a facility
    __table_args__ = (
        sa.UniqueConstraint("facility_id", "device_id", name="uq_device_facility"),
    )

    events: Mapped[list["Event"]] = relationship(
        back_populates="device", cascade="all, delete-orphan"
    )


class Event(Base):
    __tablename__ = "events"

    id: Mapped[int] = mapped_column(primary_key=True, autoincrement=True)
    # Durable cross-boot event id assigned by the backend (ADR-005)
    event_id: Mapped[str] = mapped_column(sa.String(64), index=True, nullable=False)
    device_id: Mapped[str] = mapped_column(
        sa.String(64), ForeignKey("devices.device_id"), index=True, nullable=False
    )
    type: Mapped[str] = mapped_column(sa.String(32), nullable=False)
    sensor_id: Mapped[int] = mapped_column(sa.Integer, nullable=True)
    value: Mapped[int] = mapped_column(sa.Integer, nullable=True)
    at_ms: Mapped[int] = mapped_column(sa.BigInteger, nullable=True)
    seq: Mapped[int] = mapped_column(sa.BigInteger, nullable=True)
    received_at: Mapped[str] = mapped_column(
        sa.DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    # Idempotency: one logical event per (device_id, event_id) (ADR-005, NFR-003)
    __table_args__ = (
        sa.UniqueConstraint("device_id", "event_id", name="uq_event_device_event_id"),
    )

    device: Mapped["Device"] = relationship(back_populates="events")


class OccupancySnapshot(Base):
    __tablename__ = "occupancy_snapshots"

    id: Mapped[int] = mapped_column(primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(
        sa.String(64), ForeignKey("devices.device_id"), index=True, nullable=False
    )
    count: Mapped[int] = mapped_column(sa.Integer, nullable=False)
    free_spaces: Mapped[int] = mapped_column(sa.Integer, nullable=False)
    recorded_at: Mapped[str] = mapped_column(
        sa.DateTime(timezone=True), server_default=func.now(), nullable=False
    )