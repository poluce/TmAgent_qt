# Eval Tests

这个目录放 CLI/Agent 的离线评测资产，不参与主工程编译。

包含：

- `eval_runner.py`：批量执行评测任务并输出 JSON 报告
- `eval_scorer.py`：按任务定义中的规则打分
- `tasks/`：评测任务集和样例工作区
- `requirements.txt`：评测脚本依赖

示例：

```powershell
python tests/eval/eval_runner.py --cli ./build/TmAgentCli --suite tests/eval/tasks/task_suite.yaml --output tests/eval/report.json
```
