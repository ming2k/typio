## Documentation Structure Rules

This project organizes documentation by **reader intent** (Diátaxis) plus **Architecture Decision Records** (ADRs). Follow these rules whenever you create, move, or modify a documentation file.

### Directory layout

```
docs/
├── tutorials/      Learning-oriented, step-by-step lessons that take a new user from zero to a working result
├── how-to/         Task-oriented recipes; titles always start with "How to"
├── reference/      Lookup-oriented tables for config, API, CLI flags, error codes — dense, exhaustive, no prose
├── explanation/    Understanding-oriented discussion of concepts, architecture, and design rationale
├── adr/            Architecture Decision Records — numbered, immutable, append-only
└── dev/            Contributor-oriented docs (setup, testing, release, code style) — separate from user docs
```

Root-level documentation files are limited to: `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `LICENSE`, `SECURITY.md`, `CODE_OF_CONDUCT.md`. Do not add other Markdown files at the project root.

### Routing rule (where does this content go?)

When placing or moving any document, apply this decision flow in order. Stop at the first match.

1. Records a past architectural decision (chose X over Y, decided to adopt Z) → `docs/adr/NNNN-<slug>.md`
2. Needed to set up a dev environment or contribute code → `docs/dev/`
3. Reader follows step-by-step to learn the system → `docs/tutorials/`
4. Reader is trying to accomplish a specific named task → `docs/how-to/`
5. Reader scans for a specific value (config key, API field, CLI flag) → `docs/reference/`
6. Explains why something works the way it does, or describes a concept → `docs/explanation/`
7. One-minute project pitch plus a minimal run command → root `README.md`

The guiding question is *"what is the reader trying to do right now?"*, not *"what is this content about?"*. If a document seems to belong in two places, it is two documents — split it.

### When code changes require doc changes

Update docs in the same change set as the code:

- Public API, CLI flag, or config key changed → update the relevant table in `docs/reference/`
- New user-discoverable feature added → add a "How to <use feature>" guide in `docs/how-to/`
- Install, build, or run steps changed → update `README.md` quick start and `docs/tutorials/01-getting-started.md`
- Dev environment, test commands, or release process changed → update `docs/dev/`
- Architectural decision made (chose between alternatives, changed a core pattern) → write a new ADR in `docs/adr/`
- User-visible behavior changed → add an entry under "Unreleased" in `CHANGELOG.md`
- Pure internal refactor with no user-visible effect → usually no doc change needed

When unsure whether a change is "architectural", err toward writing a brief ADR. ADRs are cheap; lost design context is expensive. Phrases like "we decided to", "we chose X over Y because", "going forward we will", "this replaces the previous approach" all signal a needed ADR.

### ADR rules

- Numbering is sequential, zero-padded to four digits (`0001`, `0002`, ...). Never reuse numbers.
- Filename pattern is `NNNN-short-slug.md` (e.g. `0007-switch-to-grpc.md`).
- Copy `docs/adr/template.md` to start a new ADR.
- Once an ADR's status is `Accepted`, do not edit its body. To change a decision, write a new ADR and update only the `Status` line of the old one to `Superseded by ADR-NNNN`.
- Aim for one page. ADRs record decisions, not designs.

### Writing style by section

Match the voice of the section you write in:

- **Tutorials** use second person ("you will..."), guarantee success, and state the expected outcome of every step.
- **How-to guides** are imperative ("Configure X by..."), and titles always start with "How to".
- **Reference** pages use tables and lists, never prose narrative, and contain no opinions — completeness over readability.
- **Explanations** are discursive and may be opinionated; they link to ADRs for specific decision history.
- **ADRs** follow the template strictly and avoid embellishment.
- **README and dev docs** are direct and scannable; they link out rather than duplicate content.

### Things to never do

- Do not create a single monolithic `Documentation.md` or `Guide.md`. Split content by Diátaxis category.
- Do not duplicate the README quick start inside `docs/`. Link to it.
- Do not add design rationale to reference pages. Move it to `docs/explanation/` or an ADR.
- Do not add option tables inside tutorials. Link to `docs/reference/` instead.
- Do not edit accepted ADRs. Supersede them with a new ADR.
- Do not mix user-facing docs and contributor docs. `docs/dev/` is the firewall.
- Do not create empty section directories without an `index.md` placeholder.
- Do not invent new top-level sections under `docs/` without explicit approval.

### When unsure

1. Look for a similar existing document in `docs/` and follow its pattern.
2. Re-read this section's routing rule and apply it strictly.
3. If still unsure, ask the user before guessing. A misplaced document is harder to fix later than a brief clarifying question now.


