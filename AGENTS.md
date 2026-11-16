
# Seyrix-Linux — Agent Instructions

## Engineering principles

- Prioritize correctness, stability, and security over speed.
- Preserve existing architecture unless a change is justified.
- Never claim a test passed without execution evidence.
- Distinguish PASS, FAIL, INCOMPLETE, and NOT RUN.
- Do not merge, push, promote an ISO, or modify physical disks without authorization.
- Preserve existing worktrees, build artifacts, and diagnostic evidence.
- Avoid unbounded debugging loops and repeated identical tests.

## Documentation delegation

For substantial engineering tasks, delegate documentation to the
`documentation` subagent when it is configured and available.

Substantial tasks include:
- New features or architectural changes.
- Bug investigations with meaningful findings.
- Phase completion or acceptance testing.
- Changes requiring engineering reports or changelogs.

Workflow:

1. The main agent performs engineering and verification.
2. Once meaningful results exist, delegate documentation.
3. Supply the documentation agent with verified results,
   changed files, test outcomes, and remaining blockers.
4. Let the documentation agent update relevant Markdown files.
5. Review the documentation changes for factual accuracy.

Do not spawn a documentation agent after every minor edit.
Do not run multiple documentation agents for the same task.

The documentation agent must not:
- Modify production source code or tests.
- Execute builds, QEMU, or destructive commands.
- Invent test results or claim unverified fixes.
- Commit, merge, push, or promote artifacts.

If subagent delegation is unavailable, document directly
and report that delegation was not used.

## Completion criteria

A development task is complete only when its required
verification passes and its documentation reflects the
actual results.

An incomplete task must retain a resumable checkpoint.
