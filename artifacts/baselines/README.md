# 最终验收基线

本目录只保存各阶段最终采用的机器可读证据；调优过程中的失败与中间结果保存在本地
`artifacts/archive/`，不纳入 Git。

- `stage1-results-20260801-232528.json`：阶段 1 正式完成基线。
- `stage1-ahk-ipc-20260801-232714.json`：阶段 1 AHK 持久 IPC。
- `stage1-results-20260801-235518.json`：阶段 2 完成时的阶段 1 性能回归。
- `stage2-results-20260801-235527.json`：阶段 2 功能验收。
- `stage2-ahk-copy-20260801-235527.diff`：隔离迁移副本与原 AHK 的差异。
- `stage3-results-20260802-121348.json`：阶段 3 功能与阶段 2 回归。
- `stage3-performance-20260802-121211.json`：阶段 3 性能回归。
- `stage4-results-20260802-130258.json`：阶段 4 功能与阶段 3/2 回归。
- `stage4-performance-20260802-130336.json`：阶段 4 性能回归。
- `stage5-functional-20260802-131459.json`：阶段 5 最终功能与阶段 4/3/2 回归。
- `stage5-ahk-ipc-20260802-131519.json`：阶段 5 隔离 AHK 持久 IPC 回归。
- `stage5-performance-20260802-131542.json`：阶段 5 最终性能回归。
- `stage5-audit.json`：目录、文档、基线、边界和 Git 状态审计。
