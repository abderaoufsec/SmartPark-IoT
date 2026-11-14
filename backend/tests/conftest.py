"""SmartPark-IoT backend test configuration (M3).

Uses an in-memory SQLite database for fast, isolated tests. PostgreSQL is
used in production; SQLite validates the same SQLAlchemy models and API
logic without requiring a running broker/DB.
"""
from typing import Generator

import pytest
from fastapi.testclient import TestClient
from sqlalchemy import create_engine, StaticPool
from sqlalchemy.orm import Session, sessionmaker

from app.main import app
from app.database import Base, get_db


@pytest.fixture(scope="function")
def db_session() -> Generator[Session, None, None]:
    """Create a fresh in-memory SQLite DB for each test.

    Uses StaticPool so all connections share the same in-memory database
    (required for SQLite :memory:).
    """
    test_engine = create_engine(
        "sqlite://",
        connect_args={"check_same_thread": False},
        poolclass=StaticPool,
    )
    Base.metadata.create_all(bind=test_engine)
    TestingSessionLocal = sessionmaker(bind=test_engine)
    session = TestingSessionLocal()
    yield session
    session.close()
    Base.metadata.drop_all(bind=test_engine)


@pytest.fixture(scope="function")
def client(db_session: Session) -> TestClient:
    """FastAPI TestClient with the test DB session injected via get_db."""

    def override_get_db() -> Generator[Session, None, None]:
        yield db_session

    app.dependency_overrides[get_db] = override_get_db
    yield TestClient(app)
    app.dependency_overrides.clear()


@pytest.fixture
def auth_headers(client: TestClient):
    """Obtain a JWT via /token and return Bearer headers."""
    resp = client.post("/token", data={"username": "tester", "password": "secret"})
    assert resp.status_code == 200, resp.text
    token = resp.json()["access_token"]
    return {"Authorization": f"Bearer {token}"}