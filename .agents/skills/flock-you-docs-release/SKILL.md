---
name: flock-you-docs-release
description: "Update and verify Flock-You README, build guides, quick-start PDFs, changelog, ADRs, hardware documentation, and release-facing support tables."
---

## Read first

Read `.clinerules/02-test-before-commit.md` and `.clinerules/05-keep-rules-current.md`. Read `docs/adr/README.md` before changing or adding a design decision. Inspect the source of every technical claim, command, board capability, environment name, pin, score, count, price, or compatibility statement you touch.

## Choose the right record

Use README and guides for user instructions. Use `CHANGELOG.md` for shipped changes and root causes. Use `.clinerules/` for durable work procedures and verified failure modes. Use a new ADR for an expensive design choice with meaningful alternatives. Supersede an ADR with a new record instead of rewriting history.

Keep public tables synchronized with code:

- Board and environment lists come from `platformio.ini` and CI.
- Detection methods and scores come from `fy_detect.h`, `fy_confidence.h`, and `.clinerules/04-detection-methods.md`.
- Config behavior comes from `fy_config.h` and ADR-0001.
- Serial fields come from the emitter, dashboard consumer, and ADR-0002.
- Hardware claims require schematics, vendor data, or physical verification.

Do not describe Markdown-only PCB material as production files if KiCad, Gerber, drill, or pick-and-place artifacts are absent. Distinguish planned hardware, documented circuits, and files ready for manufacturing.

## Verify

Check every changed relative link and fragment against tracked files and real GitHub heading slugs. Run commands from the repository root. For `docs/print/` edits, run `python3 docs/print/make_quick_start_pdfs.py`, then run it with `--check` and inspect both PDFs for clipping and readable output.

For build or flash instructions, run the safest applicable command or tie the statement to existing verified evidence. For generated support counts, recalculate instead of copying older numbers. Review the final diff for contradictions across README, guides, ADRs, changelog, and `.clinerules/`.

Report document paths, link checks, generated artifacts, source evidence, and unverified hardware claims. A documentation commit does not establish firmware release or GitHub Pages deployment.
