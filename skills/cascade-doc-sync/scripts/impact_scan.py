#!/usr/bin/env python3
"""Detect downstream docs impacted by top-level governance doc changes.

Usage:
  python3 skills/cascade-doc-sync/scripts/impact_scan.py
  python3 skills/cascade-doc-sync/scripts/impact_scan.py --changed <file1> <file2> ...
  python3 skills/cascade-doc-sync/scripts/impact_scan.py --format json
"""

import argparse
import json
import subprocess
from pathlib import Path
from typing import Dict, List, Set

TOP_DOCS = {
    "docs/00_规范/00-项目愿景与产品规划.md",
    "docs/00_规范/01-Qt技术约束与前提条件.md",
    "docs/00_规范/02-愿景落地路线图.md",
    "docs/00_规范/03-文档执行闭环规范.md",
}

BASE_TARGETS = [
    "docs/README.md",
    "docs/10_方案/10-架构升级设计方案.md",
    "docs/10_方案/11-记忆系统规划方案.md",
    "docs/10_方案/12-子模块更新与适配.md",
    "docs/10_方案/13-tree_sitter_parser_rewrite_plan.md",
]

RULE_TARGETS: Dict[str, List[str]] = {
    "docs/00_规范/00-项目愿景与产品规划.md": [
        "docs/README.md",
        "docs/10_方案/10-架构升级设计方案.md",
        "docs/10_方案/11-记忆系统规划方案.md",
        "docs/10_方案/12-子模块更新与适配.md",
        "docs/10_方案/13-tree_sitter_parser_rewrite_plan.md",
    ],
    "docs/00_规范/01-Qt技术约束与前提条件.md": [
        "docs/README.md",
        "docs/10_方案/10-架构升级设计方案.md",
        "docs/10_方案/11-记忆系统规划方案.md",
    ],
    "docs/00_规范/02-愿景落地路线图.md": [
        "docs/README.md",
        "docs/10_方案/10-架构升级设计方案.md",
        "docs/10_方案/11-记忆系统规划方案.md",
    ],
    "docs/00_规范/03-文档执行闭环规范.md": [
        "docs/README.md",
        "docs/10_方案/10-架构升级设计方案.md",
        "docs/10_方案/11-记忆系统规划方案.md",
    ],
}

EXCLUDED_PREFIXES = ("docs/20_调研/", "docs/90_归档/")


def git_changed_files() -> List[str]:
    try:
        p = subprocess.run(
            ["git", "diff", "--name-only"],
            check=True,
            capture_output=True,
            text=True,
        )
        return [line.strip() for line in p.stdout.splitlines() if line.strip()]
    except Exception:
        return []


def normalize(paths: List[str]) -> List[str]:
    out = []
    for p in paths:
        pp = Path(p).as_posix()
        if pp.startswith("./"):
            pp = pp[2:]
        out.append(pp)
    return out


def build_impact(changed: List[str]) -> Dict[str, object]:
    changed_set = set(changed)
    changed_top = sorted(changed_set & TOP_DOCS)

    targets: Set[str] = set()
    reasons: Dict[str, List[str]] = {}

    for top in changed_top:
        for t in RULE_TARGETS.get(top, BASE_TARGETS):
            if t.startswith(EXCLUDED_PREFIXES):
                continue
            targets.add(t)
            reasons.setdefault(t, []).append(top)

    return {
        "changed_files": changed,
        "changed_top_docs": changed_top,
        "targets": sorted(targets),
        "excluded_prefixes": list(EXCLUDED_PREFIXES),
        "reasons": reasons,
        "cascade_needed": bool(changed_top),
    }


def print_markdown(result: Dict[str, object]) -> None:
    print("# Cascade Impact Result")
    print()
    if not result["cascade_needed"]:
        print("- cascade_needed: no")
        print("- reason: no top-level governance docs changed")
        return

    print("- cascade_needed: yes")
    print()
    print("## Changed Top-Level Docs")
    for p in result["changed_top_docs"]:
        print(f"- `{p}`")

    print()
    print("## Downstream Targets")
    for p in result["targets"]:
        because = ", ".join(result["reasons"].get(p, []))
        print(f"- `{p}`")
        print(f"  - because: `{because}`")

    print()
    print("## Default Exclusions")
    for pre in result["excluded_prefixes"]:
        print(f"- `{pre}*`")


def main() -> int:
    parser = argparse.ArgumentParser(description="Scan cascade impact for governance doc changes.")
    parser.add_argument("--changed", nargs="*", default=None, help="Changed files (optional).")
    parser.add_argument("--format", choices=["md", "json"], default="md")
    args = parser.parse_args()

    changed = normalize(args.changed if args.changed is not None else git_changed_files())
    result = build_impact(changed)

    if args.format == "json":
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print_markdown(result)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
