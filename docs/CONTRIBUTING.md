# Contributing to AURA DAW

## Branches

- `main` — always releasable, protected, requires CI + review.
- `development` — integration branch for the next release.
- `feature/<name>` — short-lived feature branches, merged into `development`.

## Workflow

1. Fork / branch from `development`.
2. Write code + unit tests (every DSP/engine change needs coverage).
3. Format: `clang-format -i` on touched files (CI enforces `.clang-format`).
4. Build core + run tests:
   `cmake --preset core && cmake --build --preset core && ctest --preset core`.
5. Open a PR against `development`.

## Code rules

- C++20, `.hpp`/`.cpp` pairs, `#pragma once`, `Aura::*` namespaces.
- No `using namespace` in headers; left-aligned `*`/`&`.
- Real-time code (`process*`): no allocation, no locks, no I/O, no exceptions.
- Doxygen comments (`///`) on every public API.
- Warnings are errors (`/WX` / `-Werror`) — keep the build clean.
