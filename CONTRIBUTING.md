# Contributing to SmartPark-IoT

Thanks for helping. This is a two-developer project first, open to others later
— keep changes simple, reviewable and evidence-backed.

## Ground rules

1. **Milestones are gates.** Work belongs to the milestone that owns it
   (see [`docs/milestones/roadmap.md`](docs/milestones/roadmap.md)). Do not
   implement MQTT/backend/dashboard features "early" in a firmware PR.
2. **No unverified claims.** Never mark a feature done, a build passing or
   hardware validated unless you can point at a log, test output, screenshot or
   recorded measurement. "Not tested" is a perfectly good status.
3. **`legacy/v1/` is immutable.** Never modify archived files; fix things in
   `firmware/esp32/` (or later milestones) instead.
4. **Safety-relevant changes** (barrier/servo logic, pin changes, power
   distribution) must call out hardware impact in the PR description.
5. **No secrets in git.** Real credentials belong in `.env` (ignored). Use
   `.env.example` for placeholders.

## Environment setup

```bash
# Firmware (both must pass before you push)
pip install platformio           # or use the PlatformIO IDE extension
cd firmware/esp32
pio run                         # ESP32 compile (esp32dev)
pio test -e native              # host unit tests (needs a local g++/clang/MSVC)
# optional, same gate as CI:
pio check -e native "--src-filters=+<lib/smartpark_core/*>+<src/*>" \
  "--flags=--enable=warning,style,performance,portability --std=c++11 --suppress=missingIncludeSystem --inline-suppr" \
  --fail-on-defect=medium --skip-packages

# Docs-only changes need no toolchain; still check your markdown links.
```

Backend/frontend toolchains do not exist yet (milestones M3/M4); when they
land, their setup commands will be documented here.

## Branch & commit conventions

Work happens on `main` for M0 (no long-lived branches yet); from M1 onward we
follow the blueprint: feature branches + review before merge.

- Commits: `feat(firmware): …`, `fix(mqtt): …`, `docs: …`, `test: …`, `chore: …`
- One logical change per commit; message explains *why* when it is not obvious.
- Do not reformat or re-indent unrelated code in the same commit.

## Pull requests

Use the PR template. A PR is reviewable when it has:

- **Scope** — which milestone/requirement IDs (e.g. `FR-005`) it addresses.
- **Evidence** — build log snippet, test output, or an explicit "docs-only /
  not tested" statement.
- **Risk notes** — hardware, safety, data or security impact (or "none").
- **Docs updated** — README/docs/changelog updated when behavior or structure
  changes.

## Review expectations

- Both collaborators review when feasible; at least one review before merge.
- CI (`firmware-ci`) must be green for any change touching `firmware/`.
- Disagreements on architecture go into `docs/decisions/` as a short ADR
  instead of being settled in chat and forgotten.

## Reporting problems

Bugs and vulnerabilities: see [`SECURITY.md`](SECURITY.md) for private reporting.
Functional bugs and feature requests can be opened as GitHub issues using the
requirement IDs from [`docs/requirements/requirements.md`](docs/requirements/requirements.md)
when applicable.
