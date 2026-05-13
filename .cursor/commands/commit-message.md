# Commit Message

## Objective

Check the git repo and provide a vim-ready commit message in a plaintext code block, using Conventional Commits style.

## Steps

1. Run `git status` and `git diff --cached` (or `git diff` if nothing staged) to inspect changes
2. Summarize the changes
3. Output a commit message in this format (see line width below):

```
type(scope): brief description

- Change 1
- Change 2
- Change 3
```

## Line width

Inside the plaintext code block:

- **Subject (first line):** single line; prefer ~**50** characters; never more than **72**.
- **Body and bullets:** hard-wrap at **72 characters per line**; break at spaces; indent wrapped list continuations so follow-on lines align cleanly under the bullet text.

If a URL, path, or token is awkward to wrap, keep it intact on one line.

## Output

A single plaintext code block containing the full commit message, ready to paste into vim or use with `git commit -F -`.
