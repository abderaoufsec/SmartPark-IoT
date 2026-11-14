"""SmartPark-IoT backend FastAPI application (M3)."""
from contextlib import asynccontextmanager

from fastapi import FastAPI, Depends, HTTPException, status, Form
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials

from app.auth import create_access_token, decode_token
from app.config import get_settings
from app.database import engine, Base, get_db
from app.models import Device  # noqa: F401  (ensure models are registered)
from app.api import events as events_router
from app.api import status as status_router

settings = get_settings()

security = HTTPBearer()


@asynccontextmanager
async def lifespan(app: FastAPI):
    Base.metadata.create_all(bind=engine)
    yield


app = FastAPI(
    title="SmartPark-IoT Backend",
    version="0.1.0-m3",
    lifespan=lifespan,
)


def get_current_user(
    credentials: HTTPAuthorizationCredentials = Depends(security),
):
    """JWT bearer dependency. Returns TokenData or raises 401."""
    token_data = decode_token(credentials.credentials)
    if token_data is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid authentication credentials",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return token_data


@app.post("/token")
def login(username: str = Form(...), password: str = Form(...)):
    """Demo login: accepts any username/password, issues a JWT.
    In production replace with real user lookup against a users table."""
    access_token = create_access_token(data={"sub": username})
    return {"access_token": access_token, "token_type": "bearer"}


@app.get("/")
def root():
    return {"service": "smartpark-backend", "version": "0.1.0-m3"}


# Include routers (auth required for event/status endpoints)
app.include_router(
    events_router.router,
    dependencies=[Depends(get_current_user)],
)
app.include_router(
    status_router.router,
    dependencies=[Depends(get_current_user)],
)