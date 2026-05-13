# Commit Step

Use at a **natural breakpoint** while implementing a handoff or multi-step task.
Produces one **small, focused** commit.

## Steps

1. Run `git status` and `git diff --cached` (if staged) plus `git diff` for
   unstaged changes. Ensure the diff represents **one logical concern** only;
   if not, stage a subset (paths or `git add -p`) before continuing.
2. Stage only what belongs in this commit: `git add <paths>` or `git add -p`.
3. Summarize the staged diff accurately (no invented scope).
4. Output **one** plaintext fenced code block with the **full** commit message
   per `.cursor/rules/commit-message.mdc` (Conventional Commits, subject ~50
   chars / max 72, blank line, body bullets hard-wrapped at 72).
5. Commit using that exact text, for example:

```bash
git commit -F - <<'EOF'
paste the full message from the code block here
EOF
```

6. Run `git status` briefly to confirm a clean staged area or remaining work.

## Rules

- Follow `.cursor/rules/git-workflow.mdc` (single trunk, commit often during
  handoffs, no unrelated hunks in the commit).
