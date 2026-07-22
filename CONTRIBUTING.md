# Contributing to Glyph Atlas

This project follows a disciplined engineering workflow with a focus on reviewable changes and stable releases.

## Branch and commit policy

- Open feature branches from `main`.
- Keep commits focused and descriptive.
- Use present-tense, imperative commit messages.

## Development workflow

1. Create a branch named `feature/<short-description>` or `fix/<short-description>`.
2. Run `cmake -S . -B build && cmake --build build`.
3. Run tests with `ctest --test-dir build --output-on-failure`.
4. Open a pull request describing the change, motivation, and validation steps.

## Issue triage

- Bug fixes should include regression tests when possible.
- Feature additions should include API usage examples.
- Documentation updates should be clear, actionable, and avoid boilerplate.

## Code quality

- Prefer explicit error handling over opaque failure modes.
- Keep public APIs stable and backward-compatible.
- Avoid introducing unused headers or needless dependencies.
