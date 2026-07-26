# Where the documents are

One index of the planning, in-progress, and review documents, so nobody has to
guess which file answers a question. Product and contract documentation is
listed at the bottom; this file exists mainly for the first two sections.

**Read the status column before trusting a claim.** Several of these describe
intent rather than behaviour, and the difference has been expensive here
before. Nothing in this file is authoritative about what the code does — the
code and its tests are.

## Live: work in progress or waiting on a decision

| File | What it is | Status |
| --- | --- | --- |
| `EXTENSION_PLAN.md` | Architecture history, the backlog, and the **D1-D7 decision gates**. The place to look for "what is left" and "what needs a human to choose". | **Live.** Part roadmap, part changelog. A section here is *not* proof a feature exists; landed work is marked as such. D2, D5, D6 and D7 are still unanswered. |
| `.hermes/plans/2026-07-16_143000-plug-c-split.md` | Plan for extracting modules out of `src/plug.c`. | **Live, revised 2026-07-26.** Its original premise was falsified — `plug.c` grew while being split — so the goal and ordering changed. Tasks 1.2, 1.3, 1.4, 2.1, 2.2, 2.3, 2.5, 2.6 remain. |
| `cadence-overhauls-2026-07-26.md` | Notes for a future overhaul of the Cadence scene: face and colour, anti-aliasing, particle fades, its "undertuned" control set. | **Live, nothing implemented.** Explicitly a scratchpad, not a description of the scene as it stands. |
| `tools/UI_REVIEW.md` | The headless UI review loop: private Xvfb capture, the `--ui-probe` key table, and what the probe still cannot reach. | **Live and current.** Read this before reporting a UI defect. |

## Shipped: kept for the reasoning, not for the plan

Useful when you need to know *why* something is the way it is. Do not treat
their task lists as outstanding work.

| File | What it is | Status |
| --- | --- | --- |
| `.hermes/plans/2026-07-17_150000-reactivity-routing-layer.md` | Plan for the shared source→curve→parameter modulation layer under all ten scenes. | **Shipped and accepted.** Phase 4 extras deferred. |
| `.hermes/plans/2026-07-19_040000-lyrics-production-workflow.md` | Plan for synchronising authored lyrics against Whisper timing instead of transcribing them. | **Shipped**, verified hands-on on a real track. |
| `reviews/2026-07-14-ux-audit.md` | Read-only UX audit of the scene-engine foundation by four parallel sub-agents. | **Historical snapshot.** Still the evidence and rationale behind much of the current interface. |
| `reviews/2026-07-14-ux-audit-implementation.md` | The audit turned into an ordered delivery checklist. | **Historical**, delivered. |
| `reviews/2026-07-14-creative-scene-ideas.md` | Pure ideation about what this tool could do; no code was written. | **Historical, aspirational.** Ideas here were never a commitment. |
| `reviews/2026-07-14-creative-scene-implementation.md` | The bounded subset of those ideas chosen for delivery. | **Historical**, delivered. |
| `reviews/2026-07-14-implementation-review.md` | Code-level review of the two commits that implemented the audit. | **Historical.** |

## Contracts and product documentation

| File | What it is |
| --- | --- |
| `README.md` | The user-facing product: supported workflows, CLI, formats, limits. The place to correct a user-visible claim. |
| `packaging/PRODUCT_READINESS.md` | Honest platform and release boundaries. What is exercised, on what, and what is only statically reviewed. Where a limitation belongs when it is not a bug. |
| `AGENTS.md` (`CLAUDE.md` is a symlink to it) | The working guide for coding agents: conventions, product invariants, and the traps this repository has already paid for. **Untracked by design** — it is gitignored, so it does not travel with a clone. |
| `tools/ANALYSIS_ADAPTERS.md` | The optional external capabilities: Whisper import, the analysis orchestrator, MiMo/OpenRouter, and Google Fonts import. Contracts, hosts, and privacy boundaries. |
| `tools/MEASURED_ANALYSIS.md` | The measured-analysis pipeline and its schema. |
| `schemas/*.json` | Machine-readable interchange contracts, including `schemas/project-v1.schema.json` for the `.musi` format. |
| `tests/e2e/README.md` | Manual end-to-end tests — real Whisper, a live model request, the built binary under Xvfb. Excluded from every automated suite by design; never wire them into CI. |
| `CONTRIBUTING.md`, `CHANGELOG.txt` | Inherited from the upstream project. |

## Keeping this file honest

It is an index, not a record. When a document's status changes — a plan ships,
a gate is answered, a scratchpad becomes an implementation — move its row and
say so here in the same commit. A stale index is worse than none, because it
is trusted.

This file is deliberately **not** in `distribution_support_files`. It points at
`.hermes/plans/` and `reviews/`, which a release archive does not carry, so
shipping it would hand users an index of things they do not have.
