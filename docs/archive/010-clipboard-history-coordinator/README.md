# ClipboardHistoryCoordinator 重构记录

## 目标与边界

`ClipboardHistoryCoordinator` 成为 `ClipboardGateway`、`ClipboardHistoryModel` 与
`ClipboardHistoryStore` 的唯一业务协调边界，负责普通剪贴板读写代理、历史异步加载与持久化、
Windows 剪贴板监听处理、稳定性重试、自写入回声抑制、去重以及 monitor/store 错误聚合。

`EditorController` 继续保留既有 QML 属性、信号和 `Q_INVOKABLE`，并负责 IPC JSON、测试命令、
编辑缓冲区载入确认与撤销栈、窗口生命周期、状态栏和外部文件模式。历史面板 UI、存储格式与
生产/test IPC 门禁均未改变。

## 验证范围

- 无窗口协调器测试覆盖异步加载、自写入通知只抑制一次、monitor 错误优先级、`ReadLocked`
  确认清空和关闭刷新。
- 隔离 clipboard-history 验证覆盖内存 gateway、监听重试、加密落盘、连续重启、删除/清空持久化
  与损坏密文保护。
- editing、window-ui 和 system 回归继续使用原入口与原断言，验证 QML、IPC 和产物格式兼容性。
- system runner 通过仅对子进程生效的 `SCRATCHEDITOR_TEST_CLIPBOARD_BACKEND=native`
  显式选择 Win32 后端；普通 test mode 仍默认使用内存后端，生产选择逻辑不变。
