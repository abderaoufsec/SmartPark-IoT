"""SmartPark-IoT backend database layer (M3).

SQLAlchemy engine + session factory. PostgreSQL via psycopg2.
"""
from sqlalchemy import create_engine
from sqlalchemy.orm import declarative_base, sessionmaker

from app.config import get_settings

settings = get_settings()

engine = create_engine(
    settings.database_url,
    pool_pre_ping=True,
    future=True,
)

SessionLocal = sessionmaker(
    autocommit=False,
    autoflush=False,
    bind=engine,
    future=True,
)

Base = declarative_base()


def get_db():
    """Yield a SQLAlchemy session (FastAPI dependency)."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()