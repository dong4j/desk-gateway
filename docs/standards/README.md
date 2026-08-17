# Desk Gateway development standards

**Language:** English · [简体中文](./README.zh-CN.md)

This directory holds mandatory development rules for Desk Gateway. Every AI agent must read and follow every file here before analyzing, changing, testing, or committing code.

## Standards

- [Git commit convention](git-commit-convention.md): commit splitting, message format, pre-commit checks, staging bounds, and remote-operation limits.

## How to apply them

1. These rules cover firmware, Web, mobile, Watch, scripts, tests, and docs.
2. After a new standards file is added, later agents must follow it too. Reading only this index is not enough.
3. If a rule here conflicts with a system instruction or an explicit user instruction, follow the higher-priority instruction. Otherwise these project rules are mandatory.
4. If implementation drifts from a rule, fix the drift before claiming the task done.
