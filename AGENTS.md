# Local application delivery

## Mandatory change verification

- Follow `docs/architecture/CHANGE_GATES.md`. Run `scripts/run_change_gates.py --static-only` after structural edits and the full acceptance command before delivering changed application binaries.
- Do not bypass `structure_contract_gate`, expand architecture baselines, suppress failures/skips, or weaken checks to make a change pass. Report blockers and fix the cause.
- Changes to gates, their tests, CI or CMake require explicit review of the rule change and negative tests demonstrating rejection of forbidden changes. A static pass alone is not behavioral or release acceptance.

## Version 2.1.2 correction and testing period

- Keep version 2.1.2 while corrections and testing continue, unless the user explicitly requests another version.
- Make reproducible changes in source, build and verify them in `build-single-instance-audit`, and deliver the resulting runtime only to the existing `dist` directory.
- Do not additionally patch the installed copy in `Program Files` during this period unless explicitly requested. Use `dist/CarServicePointG.exe` for testing the current application.
- Build the next updated 2.1.2 installer from the verified current `dist` inventory, retaining all previously agreed installer data-protection requirements. Do not patch binaries directly or leave source changes out of the reproducible build.

## General delivery rules

- The user requires one current desktop application directory: `dist`.
- The launch target is `dist/CarServicePointG.exe`. Update this existing distribution for future fixes; do not create persistent `dist-*`, dated, or task-specific application copies.
- Reuse `build-single-instance-audit` for local builds and isolated tests. Build and test outputs are development artifacts, not alternative user distributions.
- Keep the current application version for ordinary local fixes unless the user requests a version change. Public signed releases follow their separate release requirements.
- Before replacing application binaries, check for running CarServicePointG processes. Let the user save and close the application; never force-terminate it to deploy an update.
- Preserve working databases, settings, documents, attachments, and data backups. Inspect an existing distribution for such files before removing obsolete runtime files.
- Verify the updated runtime inventory and hashes with `scripts/check_runtime_package.py`. Temporary staging is allowed only during replacement and must not remain as another application directory after successful delivery.
- Keep the project shortcut pointed at `dist/CarServicePointG.exe` and provide that same launch path to the user.
- Keep the current distributable Android APK at `Output/mobile/CarServicePointG-Mobile.apk`; replace it after a verified mobile build instead of accumulating versioned APK copies. Preserve normal Android build outputs and metadata.
- Do not use `scripts/build_deploy_run_autoroot.ps1` for routine local updates: it force-stops processes, deletes the distribution, and uses an obsolete executable name. Use the existing build and a checked in-place runtime update instead.

## Persistent reliability acceptance plan REL-212

- For reliability work, read docs/reliability/PLAN.md and docs/reliability/STATUS.md first. Continue this existing plan rather than creating a replacement.
- Maintain docs/reliability/MATRIX.csv with scenario-level evidence and update STATUS.md after each work session: completed work, open defects, exact build/test evidence, running jobs and next action.
- Preserve historical results. Mark affected checks RETEST_REQUIRED after changes; code changes alone do not close a defect. A later passing retry does not erase an earlier failure.
- Pauses and unrelated tasks do not complete this plan. Keep it open until its final-build acceptance criteria are met; do not claim all functions or absolute reliability from a partial test suite.