# Create Handoff Document

## Objective

Output a single handoff document into `docs/handoffs/` for another agent or session. Use date-coded, lowercase file naming.

## Output Location and Naming

- **Path**: `docs/handoffs/`
- **Format**: `YYYYMMDD_<topic-slug>.md` (e.g. `20260315_scroll-passthrough-arch-revision.md`)
- **If multiple handoffs for the same date and topic exist**: Use `20260315b_<topic-slug>.md`, `20260315c_<topic-slug>.md`, etc.

## Steps

1. **Determine filename**: Use today's date (YYYYMMDD) and a lowercase slug for the topic. Check `docs/handoffs/` for existing files with the same date+slug; if present, use the next letter suffix (`b`, `c`, …).
2. **Gather context**: From the current conversation or user input, collect (at minimum per `.cursor/rules/handoff-documents.mdc`):
   - Current status (where work stopped; for bugs: reproducible? last behavior?)
   - Problem, feature, or goal (including reproduction steps for bugs)
   - What was tried and why it failed (when applicable)
   - Context: relevant files, APIs, constraints
   - Success criteria
   - References (docs, forums), or note “none yet”
3. **Write the document**: Follow `.cursor/rules/handoff-documents.mdc` (required sections always; extended sections when they add signal).
4. **Output**: Write the single file to `docs/handoffs/<filename>.md`.

## Output

- One handoff document at `docs/handoffs/YYYYMMDD_<topic-slug>.md` (or `YYYYMMDDb_<topic-slug>.md`, etc. if duplicates exist)
- Confirm the path used
