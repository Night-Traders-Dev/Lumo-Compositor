# Changelog

All notable changes to this project will be documented in this file.

## [0.0.1] - 2026-03-26
- Established the compositor repository structure.
- Added the initial compositor scaffold and architecture docs.
- Set the project version baseline to `0.0.1`.

## [0.0.2] - 2026-03-26
- Added the first mobile-first input pipeline: touch buffering, edge gesture capture, replay-on-release, and shell-vs-app surface classification.
- Wired the compositor for pointer, keyboard, touch, and gesture signals with wlroots seat integration.
- Added a C test harness for compositor state helpers and a repository-level `.gitignore`.
