#!/usr/bin/env python3
"""
TmAgent Eval Runner — 批量执行评测任务并生成报告

Usage:
    python eval_runner.py --cli ./build/TmAgentCli --suite eval/tasks/task_suite.yaml --output eval/report.json
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import yaml

from eval_scorer import score_task


def load_suite(suite_path: str) -> dict:
    with open(suite_path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def run_single_task(cli_path: str, task_def: dict, defaults: dict, workspace: str) -> dict:
    """执行单个评测任务，返回 CLI 的 JSON 输出 + 元信息"""
    timeout_ms = task_def.get("timeout_ms", defaults.get("timeout_ms", 120000))
    model_config = task_def.get("model_config", defaults.get("model_config", "./resources/models.yaml"))
    config_id = task_def.get("config_id", defaults.get("config_id", ""))

    # 替换 {workspace} 占位符
    task_text = task_def["task"].replace("{workspace}", workspace)

    cmd = [
        cli_path,
        "--model-config", model_config,
        "--timeout", str(timeout_ms),
        "--workspace", workspace,
        "--verbose",
        task_text,
    ]
    if config_id:
        cmd.insert(3, "--config-id")
        cmd.insert(4, config_id)

    task_id = task_def["id"]
    print(f"  [{task_id}] Running: {task_def['name']}...", file=sys.stderr)

    start = time.time()
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_ms / 1000 + 10,  # 额外 10s 缓冲
        )
        elapsed = time.time() - start

        # 解析 stdout JSON
        try:
            output = json.loads(result.stdout)
        except json.JSONDecodeError:
            output = {
                "success": False,
                "response": result.stdout[:2000] if result.stdout else "",
                "tool_calls": [],
                "elapsed_ms": int(elapsed * 1000),
            }

        return {
            "task_id": task_id,
            "exit_code": result.returncode,
            "output": output,
            "stderr": result.stderr[:2000] if result.stderr else "",
            "wall_time_s": round(elapsed, 2),
        }

    except subprocess.TimeoutExpired:
        return {
            "task_id": task_id,
            "exit_code": 2,
            "output": {
                "success": False,
                "response": "Process timeout",
                "tool_calls": [],
                "elapsed_ms": timeout_ms,
            },
            "stderr": "",
            "wall_time_s": timeout_ms / 1000,
        }
    except FileNotFoundError:
        print(f"  ERROR: CLI binary not found: {cli_path}", file=sys.stderr)
        sys.exit(1)


def run_suite(cli_path: str, suite: dict, workspace: str) -> list[dict]:
    defaults = suite.get("defaults", {})
    results = []

    for task_def in suite.get("tasks", []):
        run_result = run_single_task(cli_path, task_def, defaults, workspace)
        score = score_task(task_def, run_result)
        run_result["score"] = score
        results.append(run_result)
        status = "PASS" if score["passed"] else "FAIL"
        print(
            f"  [{run_result['task_id']}] {status} — "
            f"score: {score['total']}/{score['max']} "
            f"({run_result['wall_time_s']}s)",
            file=sys.stderr,
        )

    return results


def build_report(suite: dict, results: list[dict]) -> dict:
    total_score = sum(r["score"]["total"] for r in results)
    max_score = sum(r["score"]["max"] for r in results)
    passed = sum(1 for r in results if r["score"]["passed"])

    return {
        "suite_version": suite.get("version", "unknown"),
        "summary": {
            "total_tasks": len(results),
            "passed": passed,
            "failed": len(results) - passed,
            "total_score": total_score,
            "max_score": max_score,
            "score_pct": round(total_score / max_score * 100, 1) if max_score > 0 else 0,
        },
        "results": results,
    }


def main():
    parser = argparse.ArgumentParser(description="TmAgent Eval Runner")
    parser.add_argument("--cli", required=True, help="Path to TmAgentCli binary")
    parser.add_argument("--suite", required=True, help="Path to task_suite.yaml")
    parser.add_argument("--output", default="eval/report.json", help="Output report path")
    parser.add_argument("--workspace", default=None, help="Override workspace directory")
    args = parser.parse_args()

    suite = load_suite(args.suite)

    # 默认 workspace 为 suite 文件同目录下的 example_workspace
    workspace = args.workspace or str(
        Path(args.suite).parent / "example_workspace"
    )
    workspace = os.path.abspath(workspace)

    print(f"Eval suite: {args.suite}", file=sys.stderr)
    print(f"Workspace:  {workspace}", file=sys.stderr)
    print(f"Tasks:      {len(suite.get('tasks', []))}", file=sys.stderr)
    print("---", file=sys.stderr)

    results = run_suite(args.cli, suite, workspace)
    report = build_report(suite, results)

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print("---", file=sys.stderr)
    s = report["summary"]
    print(
        f"Done: {s['passed']}/{s['total_tasks']} passed, "
        f"score {s['total_score']}/{s['max_score']} ({s['score_pct']}%)",
        file=sys.stderr,
    )
    print(f"Report: {args.output}", file=sys.stderr)

    sys.exit(0 if s["failed"] == 0 else 1)


if __name__ == "__main__":
    main()
