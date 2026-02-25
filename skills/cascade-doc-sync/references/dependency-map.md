# Dependency Map

## Top-Level Governance Docs

1. `docs/00_规范/00-项目愿景与产品规划.md`
2. `docs/00_规范/01-Qt技术约束与前提条件.md`
3. `docs/00_规范/02-愿景落地路线图.md`
4. `docs/00_规范/03-文档执行闭环规范.md`

## Downstream Update Targets (Default)

1. `docs/README.md`
2. `docs/10_方案/10-架构升级设计方案.md`
3. `docs/10_方案/11-记忆系统规划方案.md`
4. `docs/10_方案/12-子模块更新与适配.md`
5. `docs/10_方案/13-tree_sitter_parser_rewrite_plan.md`

## Exclusion Rules (Default)

1. Do not update `docs/20_调研/*`.
2. Do not update `docs/90_归档/*`.

## Impact Rules

### If `00-项目愿景与产品规划.md` changes

Update:

1. `docs/README.md`
2. `docs/10_方案/10-架构升级设计方案.md`
3. `docs/10_方案/11-记忆系统规划方案.md`
4. Other specialized plan docs if terminology/scope changed

### If `01-Qt技术约束与前提条件.md` changes

Update:

1. `docs/README.md` (if read order/rules changed)
2. `docs/10_方案/10-架构升级设计方案.md`
3. `docs/10_方案/11-记忆系统规划方案.md` (only technical-boundary sections)

### If `02-愿景落地路线图.md` changes

Update:

1. `docs/README.md` (execution entry/order)
2. `docs/10_方案/10-架构升级设计方案.md` (milestones, acceptance)
3. Specialized plan docs whose milestones/acceptance criteria are impacted

### If `03-文档执行闭环规范.md` changes

Update:

1. `docs/README.md` (governance and maintenance rules)
2. `docs/10_方案/10-架构升级设计方案.md` (execution loop references)
3. Specialized plan docs only where workflow/closure sections exist
