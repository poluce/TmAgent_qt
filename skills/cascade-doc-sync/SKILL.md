---
name: cascade-doc-sync
description: Automatically cascade updates from top-level governance docs to downstream implementation docs in this repo. Use when docs/00_规范/00-项目愿景与产品规划.md or other top-level governance docs are changed and dependent docs under docs/10_方案 and docs/README.md must be checked and updated while skipping docs/20_调研 by default.
---

# Cascade Doc Sync

## Overview

Apply a deterministic doc-update workflow: when top-level governance docs change, identify impacted downstream docs, update them in order, and close the loop with a concise sync report.

## Workflow

1. Detect changed top-level docs.
2. Resolve impacted downstream docs from the dependency map.
3. Update downstream docs in strict order.
4. Validate links, versions, and execution consistency.
5. Output a sync report with changed files and open items.

## Step 1: Detect Governance Changes

Run:

```bash
python3 skills/cascade-doc-sync/scripts/impact_scan.py
```

Or specify changed files explicitly:

```bash
python3 skills/cascade-doc-sync/scripts/impact_scan.py \
  --changed docs/00_规范/00-项目愿景与产品规划.md
```

If no top-level docs changed, stop and report "no cascade needed".

## Step 2: Build Impact Set

Read `references/dependency-map.md` and use the script output as the source of truth for target files.

Default behavior:

1. Update `docs/10_方案/*` and `docs/README.md`.
2. Do not edit `docs/20_调研/*` unless user explicitly asks.
3. Do not edit `docs/90_归档/*` unless doing archive maintenance.

## Step 3: Update in Order

Always update in this order:

1. `docs/README.md` (entry and execution path)
2. `docs/10_方案/10-架构升级设计方案.md` (main execution plan)
3. Other `docs/10_方案/*` (specialized implementation docs)

Update rules:

1. Keep scope aligned with the top-level change; avoid unrelated rewrites.
2. Keep wording consistent with governance terms.
3. Update references, phase names, acceptance criteria, and boundary statements.
4. Remove conflicting or duplicated statements rather than keeping dual wording.

## Step 4: Validate

Minimum checks:

```bash
# stale legacy path/name check
rg -n "docs/项目愿景与产品规划.md|docs/架构升级设计方案.md|docs/愿景落地路线图.md" docs -S

# ensure downstream references exist
rg -n "00_规范|10_方案|03-文档执行闭环规范" docs/README.md docs/10_方案 -S
```

If checks find conflicts, fix and rerun checks.

## Step 5: Close the Loop

Return a short report including:

1. Top-level docs detected as changed.
2. Downstream docs updated.
3. Docs intentionally skipped (especially `docs/20_调研/*`).
4. Remaining open items requiring user decision.

## Resources

1. Dependency map: `references/dependency-map.md`
2. Impact scanner: `scripts/impact_scan.py`
