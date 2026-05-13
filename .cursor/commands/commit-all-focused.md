# Commit All (Focused Plan)

Use when the working tree has **multiple** logical changes and you want them
checked in as **several small commits**, not one lump. This command **inspects**
the repo, **plans** a commit sequence aligned with `.cursor/rules/git-workflow.mdc`,
then **performs** `git add` / `git commit` until everything appropriate is
committed.

## Scope

- **In:** tracked modifications, deletions, renames, and **untracked files that
  are not ignored** (`git status` / `git ls-files --others --exclude-standard`).
- **Out:** do **not** `git push` unless the user explicitly asks (trunk policy in
  `.cursor/rules/git-workflow.mdc`).
- **Judgment:** do not commit build artifacts, editor junk, or other generated
  noise unless the user asked to track them — same bar as staging “narrowly” in
  the workflow rules. If something looks wrong to stage, say why and skip or
  ask once.

## Phase 1 — Inspect

Run and use the results to partition work:

1. `git status` (short branch + summary helps: `git status -sb`).
2. Staged vs unstaged: `git diff --cached`, then `git diff` for unstaged
   tracked changes.
3. Untracked (not ignored): `git ls-files --others --exclude-standard` or
   equivalent; spot-check large or unusual paths.

If there is nothing to commit, report that and stop.

## Phase 2 — Plan (small, focused commits)

Before any `git add`, write a **short commit plan** (order + grouping + one-line
rationale per commit). Follow:

- **Small, focused commits** — one logical concern per commit; prefer several
  commits over one mixed commit (see `.cursor/rules/git-workflow.mdc`).
- **Stage narrowly** — group by concern using explicit paths or `git add -p` when
  a file mixes unrelated edits.

Typical grouping patterns (pick what fits the diff):

- Separate **docs** from **code** from **tooling/config** when they are
  independent.
- Separate **behavior change** from **rename/move** or **format-only** when
  splitting improves history.
- Keep a **handoff-driven** implementation traceable: optional mention of
  `docs/handoffs/<name>.md` in a commit body when relevant.

Order commits so each step is coherent (e.g. scaffolding before features that
depend on it).

## Phase 3 — Execute (repeat until done)

For **each** planned commit, run the same mechanical sequence as **Commit step**
(`.cursor/commands/commit-step.md`):

1. Stage **only** that commit’s paths (or `git add -p` for partial files).
2. Summarize the **staged** diff accurately; do not invent scope.
3. Write **one** plaintext fenced code block with the **full** commit message per
   `.cursor/rules/commit-message.mdc` (Conventional Commits; subject ~50 chars /
   max 72; blank line; body wrapped at 72).
4. Commit with that exact text, e.g. `git commit -F - <<'EOF' ... EOF`.
5. Run `git status` and continue with the next group until no staged/uncommitted
   work remains for the planned set.

After the last commit, run `git status` again. The working tree should be
**clean** for all changes you intended to include. If something was deliberately
left out (noise, user-owned secrets), state that explicitly.

## If stuck

- **Mixed hunks:** prefer `git add -p` or split edits across commits after
  `git reset -p` / selective staging — still one logical concern per commit.
- **Unsure grouping:** prefer an extra small commit over a vague large one; use
  `chore` / honest WIP subjects only when necessary (see git-workflow session
  hygiene).

## Anti-patterns

- One giant commit that bundles unrelated docs, features, and config.
- Staging entire directories when only two files belong together.
- Commit messages that do not match what is actually staged.
