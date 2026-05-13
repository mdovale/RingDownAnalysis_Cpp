# No Git Mutations

Use when the user wants work **without** changing Git state on disk or on the
remote. Treat the repo as **read-only** for version control until they say
otherwise.

## Hard rules for this task / session

Do **not** run or suggest the user run any of:

- `git add`, `git restore --staged`, `git rm` (when it stages), `git mv` (when
  it stages)
- `git commit`, `git merge`, `git rebase`, `git cherry-pick`, `git revert`
- `git push`, `git pull` (merge or rebase)
- Destructive or state-changing: `git reset --hard`, `git clean -fd`,
  `git stash push` / `git stash pop` / `git stash apply`, `git branch -D`,
  `git tag -d`, `git push --force`, submodule updates that commit

Do **not** edit files under `.git/` or use tools that stage/commit as a side
effect.

## Allowed

Read-only inspection is fine, for example: `git status`, `git diff`, `git log`,
`git show`, `git branch` (list only), `git rev-parse`. You may use `git fetch`
when you need up-to-date remote-tracking refs for comparison; do **not** follow
it with `git merge` / `git rebase` / `git pull`.

## Output

Summarize changes as file paths and diffs in the chat (or the user’s preferred
artifact). If a commit message would have been appropriate, you may still output
**proposed** message text in a fenced block labeled as **not committed** — but
do not run `git commit`.

## If the user later asks to commit

They can revoke this mode explicitly (e.g. “ok to commit now”) or use **Commit
step** (`.cursor/commands/commit-step.md`).
