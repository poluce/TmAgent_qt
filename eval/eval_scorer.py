#!/usr/bin/env python3
"""
TmAgent Eval Scorer — 评分逻辑

根据任务定义中的 expected 和 scoring 配置，对 CLI 输出进行评分。
"""

from __future__ import annotations

import os
import re


def score_task(task_def: dict, run_result: dict) -> dict:
    """
    对单个任务的执行结果评分。

    返回:
        {
            "total": int,
            "max": int,
            "passed": bool,
            "dimensions": { "dim_name": {"score": int, "max": int, "detail": str}, ... }
        }
    """
    expected = task_def.get("expected", {})
    scoring = task_def.get("scoring", {})
    output = run_result.get("output", {})

    dimensions = {}

    # ─── 工具正确性 ───
    if "tool_correctness" in scoring:
        max_pts = scoring["tool_correctness"]
        dim = _score_tool_correctness(expected, output, max_pts)
        dimensions["tool_correctness"] = dim

    # ─── 输出质量 ───
    if "output_quality" in scoring:
        max_pts = scoring["output_quality"]
        dim = _score_output_quality(expected, output, max_pts)
        dimensions["output_quality"] = dim

    # ─── 效率 ───
    if "efficiency" in scoring:
        max_pts = scoring["efficiency"]
        dim = _score_efficiency(expected, output, max_pts)
        dimensions["efficiency"] = dim

    # ─── 文件验证 ───
    if "file_verification" in scoring:
        max_pts = scoring["file_verification"]
        dim = _score_file_verification(expected, run_result, max_pts)
        dimensions["file_verification"] = dim

    total = sum(d["score"] for d in dimensions.values())
    max_total = sum(d["max"] for d in dimensions.values())
    # 通过阈值：得分 >= 60%
    passed = total >= max_total * 0.6 if max_total > 0 else False

    return {
        "total": total,
        "max": max_total,
        "passed": passed,
        "dimensions": dimensions,
    }


def _score_tool_correctness(expected: dict, output: dict, max_pts: int) -> dict:
    """检查是否使用了预期的工具"""
    expected_tools = set(expected.get("tools_used", []))
    if not expected_tools:
        return {"score": max_pts, "max": max_pts, "detail": "No tool requirement"}

    actual_tools = set()
    for tc in output.get("tool_calls", []):
        tool_name = tc.get("tool", "")
        if tool_name:
            actual_tools.add(tool_name)

    matched = expected_tools & actual_tools
    ratio = len(matched) / len(expected_tools) if expected_tools else 1.0
    score = round(max_pts * ratio)

    missing = expected_tools - actual_tools
    detail = f"Used {len(matched)}/{len(expected_tools)} expected tools"
    if missing:
        detail += f", missing: {', '.join(sorted(missing))}"

    return {"score": score, "max": max_pts, "detail": detail}


def _score_output_quality(expected: dict, output: dict, max_pts: int) -> dict:
    """检查输出是否包含预期关键词"""
    keywords = expected.get("output_contains", [])
    if not keywords:
        return {"score": max_pts, "max": max_pts, "detail": "No keyword requirement"}

    response = output.get("response", "").lower()
    matched = [kw for kw in keywords if kw.lower() in response]
    ratio = len(matched) / len(keywords) if keywords else 1.0
    score = round(max_pts * ratio)

    missing = [kw for kw in keywords if kw.lower() not in response]
    detail = f"Matched {len(matched)}/{len(keywords)} keywords"
    if missing:
        detail += f", missing: {', '.join(missing)}"

    return {"score": score, "max": max_pts, "detail": detail}


def _score_efficiency(expected: dict, output: dict, max_pts: int) -> dict:
    """检查工具调用次数是否在合理范围内"""
    max_calls = expected.get("max_tool_calls", 0)
    actual_calls = len(output.get("tool_calls", []))

    if max_calls <= 0:
        return {"score": max_pts, "max": max_pts, "detail": "No efficiency requirement"}

    # 完成的工具调用数（started 不算，只算 completed）
    completed = sum(
        1 for tc in output.get("tool_calls", [])
        if tc.get("status") == "completed"
    )

    if completed <= max_calls:
        score = max_pts
        detail = f"{completed} tool calls (limit: {max_calls})"
    elif completed <= max_calls * 2:
        # 超出但不超过 2 倍，部分得分
        ratio = 1.0 - (completed - max_calls) / max_calls
        score = max(0, round(max_pts * ratio))
        detail = f"{completed} tool calls, over limit {max_calls} (partial)"
    else:
        score = 0
        detail = f"{completed} tool calls, far over limit {max_calls}"

    return {"score": score, "max": max_pts, "detail": detail}


def _score_file_verification(expected: dict, run_result: dict, max_pts: int) -> dict:
    """验证文件操作结果（检查文件是否存在、内容是否匹配）"""
    checks = expected.get("file_checks", [])
    if not checks:
        return {"score": max_pts, "max": max_pts, "detail": "No file checks"}

    passed = 0
    details = []

    for check in checks:
        path = check.get("path", "")
        if not path:
            continue

        if check.get("exists", False):
            if os.path.exists(path):
                passed += 1
                details.append(f"{path}: exists OK")
            else:
                details.append(f"{path}: NOT FOUND")
                continue

        if "contains" in check and os.path.exists(path):
            try:
                with open(path, "r", encoding="utf-8") as f:
                    content = f.read()
                pattern = check["contains"]
                if re.search(pattern, content):
                    passed += 1
                    details.append(f"{path}: content match OK")
                else:
                    details.append(f"{path}: content mismatch")
            except Exception as e:
                details.append(f"{path}: read error: {e}")

    total_checks = len(checks)
    ratio = passed / total_checks if total_checks > 0 else 1.0
    score = round(max_pts * ratio)

    return {"score": score, "max": max_pts, "detail": "; ".join(details) if details else "OK"}
