# ADR-0003 — P2 legacy-tail cleanup gate and synchronous-path quarantine

## Status
Accepted — P2 cleanup/control pass for **“Мертві хвости, legacy і fallback” п3–п4**.

## P2 legacy-tail cleanup gate
The previous inventory pass classified `legacy`, `fallback`, `MissingMethods`, `transitional`, and `allowlist` markers. This pass adds the enforcement layer for deletion safety and synchronous-path containment.

## compile-check policy
Runtime deletion is allowed only when all of the following are true:
1. the candidate is already classified in ADR-0002;
2. grep/call-graph verification shows no production caller or shows a replacement caller path;
3. a local Windows/Qt compile-check or CI compile-check passes after deletion;
4. the relevant regression/smoke tests are updated or explicitly documented as not applicable.

No runtime deletion was performed in this pass because the available container does not provide the Windows/Qt toolchain required for a real compile-check. This keeps p3 safe: code is not removed without the required proof.

## grep/call-graph verification
The cleanup gate performs a static grep/call-graph verification pass over project text/source files. It is intentionally conservative: it proves that the check ran, blocks undocumented delete candidates, and prevents uncontrolled synchronous fallback resurrection.

Static reference probe: PASS

## P4 synchronous-path policy
Old synchronous methods that duplicate or bypass async paths must either be deleted after the p3 evidence workflow or marked as internal/test-only. Production UI code must not call these paths directly.

Controlled markers:
- `CS_INTERNAL_BLOCKING_SYNC_ALLOWED` — an internal blocking process wait is retained, bounded, and documented.
- `CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY` — a synchronous fallback API remains visible only as a core/internal compatibility entry point; UI must use the async/job facade.

## Current synchronous quarantine list
| Path | Status | Reason |
|---|---|---|
| `src/core/services/settings/SyncSettingsService.cpp` | internal-only bounded sync | Windows Wi-Fi profile discovery; not UI layer; timeout + kill fallback added. |
| `src/core/services/legacyimport/LegacyImportSourceResolver.cpp` | internal-only bounded sync | One-time legacy archive extraction; timeout + kill fallback added. |
| `src/core/import/InvoiceImportEngine.h` | internal-only fallback API | Synchronous import fallback remains behind core/import; UI must use ImportJob. |

## Decision
- No legacy/fallback runtime code is deleted in this pass.
- Deletion is blocked unless ADR evidence, grep/call-graph verification, and compile-check are available.
- Synchronous fallback entry points are quarantined by explicit markers and a build/script gate.

## Follow-up
P2 p5–p6 allowlist-budget and CI-marker enforcement is documented in ADR-0004.
