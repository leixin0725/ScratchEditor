# 测试目录

## 自动验收程序

- `perf_main.cpp`：阶段 1 性能、真实 OS 输入和微软拼音验收客户端。
- `stage2_main.cpp`：滚动条、Escape、焦点和剪贴板异常回归。
- `stage3_main.cpp`：Markdown 高亮、编辑命令、查找替换和快捷键回归。
- `stage4_main.cpp`：设置页、主题字体、集中配置与明确排除项回归。

这些程序只连接由脚本启动的 `--test-mode` 隔离实例。生产 IPC 不暴露测试命令。

## AHK 夹具

`fixtures/KeysRedirect.Stage1Test.ahk` 只验证持久命名管道，不包含其他业务热键。它从
以下环境变量读取隔离目标：

- `SCRATCHEDITOR_SERVER_NAME`
- `SCRATCHEDITOR_EXE`

运行入口是 `../scripts/test-ahk-ipc.ps1`。夹具和迁移参考副本都不能覆盖用户原始
`KeysRedirect.ahk`。

## 推荐执行顺序

1. `cmake --build --preset stage4`
2. `scripts/run-stage4-tests.ps1`
3. `scripts/test-ahk-ipc.ps1`
4. `scripts/run-stage1-tests.ps1`（完整性能门槛）

最终结果应复制到 `artifacts/baselines/`；普通运行产生的时间戳结果默认被 Git 忽略。
