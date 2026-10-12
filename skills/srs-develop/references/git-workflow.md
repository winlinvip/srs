# Git Worktree and Workflow

**Scope:** The single owner of the SRS skill's git worktree and commit rules. Load this file whenever a task creates a worktree or a commit; do not restate these rules elsewhere.

## Git Worktree

Create a git worktree only when the user asks.

Put it next to the SRS checkout and name it `srs-<topic>`, for example `~/projects/srs-pr-4757` for `~/projects/srs`, so relative symlinks such as `oryx/` keep working.

Copy `trunk/objs/Platform-*/3rdparty` from the SRS checkout into the worktree, so `./configure` reuses the built third-party libraries. Do not copy the rest of `objs`, or `make` keeps objects built from the other checkout. Skip the copy when the worktree changes `trunk/3rdparty/`.

## Git Workflow

Apply these rules whenever a task produces a commit:

- Identify the owning repository before inspecting or committing staged changes. Use the current repository for SRS and skills, and `git -C` with the project-root-relative path for the other repositories:
  - `git -C oryx/` for Oryx
  - `git -C dev-docker/` for Dev Docker
  - `git -C state-threads/` for State Threads
  - `git -C website/` for the website
- Never run `git add`; the user stages the files they approve.
- Never run `git push` unless the user explicitly asks.
- Commit only when the user explicitly asks.
- Before committing, run the owning repository's staged diff, understand every staged change, and write an appropriate title and description. Do not include staged changes from another repository in the same commit.
- Prefix the commit title with the tool that made the changes: `OpenClaw:`, `Claude:`, `Codex:`, or `Kiro:`.
- If Claude made changes, use this commit message format. End it with the co-author line the Claude Code harness gives; it names the current model, so do not hard-code one:
  ```
  Commit title.

  Commit description.

  ---------

  <co-author line from the harness>
  ```
- If Codex made changes, use this commit message format. Replace `<current model name>` with the model name provided by the Codex harness; do not hard-code a model or guess an unavailable name. If the harness does not provide the model name, use `Codex` alone as the display name. Keep the bot email unchanged:
  ```
  Commit title.

  Commit description.

  ---------

  Co-authored-by: Codex (<current model name>) <199175422+chatgpt-codex-connector[bot]@users.noreply.github.com>
  ```
- If Kiro made changes, use the same title and description, with no fixed trailer.
