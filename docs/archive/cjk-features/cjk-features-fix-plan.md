# CJK 输入优化缺陷修复计划

> 状态：已完成调查和需求确认，尚未实施修复  
> 计划基线：`f02b22f6b8bed41c2f2b1818c286973ff10b4678`  
> 编写日期：2026-08-04  
> 目标读者：在全新上下文中接手实现、测试和验收的开发者或 Codex session

## 1. 文档用途

本文档是 `f02b22f` 所引入 CJK 输入优化功能的修复执行手册。接手者不应依赖之前的聊天记录；开始工作前应完整阅读本文，然后按“实施顺序”和“验收清单”执行。

本计划同时覆盖：

1. 已确认缺陷和精确复现结果。
2. 用户已经确认、不可再自行改变的产品决策。
3. ScratchEditor 的相关架构、事件流、构建方式和测试隔离方式。
4. 推荐的目标代码结构、数据结构和算法。
5. 逐文件修改范围。
6. 纯逻辑测试、真实键盘/IME 集成测试、回归测试和人工验收。
7. 性能、光标、选区、撤销和 Markdown 边界风险。

本文不是泛泛的建议。除非仓库在基线之后发生了架构变化，否则实现者应把本文的 MUST/必须项视为验收要求。

---

## 2. 已锁定的产品决策

以下三项已经由用户明确确认，实施时不要再次猜测或恢复旧行为。

### 2.1 保护区内禁用全部本次新增的自动改写

在以下保护区内部：

- 行内代码：反引号 code span；
- 行内公式：单美元符号 `$...$`；
- 围栏代码块：反引号或波浪线 fence；
- 块级公式：多行 `$$` 块和单行 `$$...$$`；

必须禁用本次 CJK 功能新增的所有自动文本改写：

- CJK 后 ASCII 标点转全角；
- `...`、`。。。` 和混合句点转 `……`；
- CJK 后 `--` 转 `——`；
- CJK–ASCII 自动空格；
- `Alt+F` 对保护区内容的整理。

保护区外侧的行内代码/公式边界仍允许补空格，例如：

```text
中文`code`中文  ->  中文 `code` 中文
公式$x+1$成立   ->  公式 $x+1$ 成立
```

“保护全部自动改写”只针对本次 CJK 功能。不要顺带重写既有 Markdown 配对、Tab 跳出、列表回车等无关功能；这些功能必须保持回归通过。

### 2.2 支持单行块级公式

必须把以下形式识别为只保护当前行的块级公式：

```text
$$x+1$$
$$ x + y $$
```

单行块公式不能切换多行公式状态，也不能导致后续普通行被错误保护。

多行块公式继续使用仅含 `$$` 和可选空白的独立分隔行：

```text
$$
中文ABC+x1
$$
```

建议采用以下确定语法，避免旧实现的任意行误切换：

- 单行形式：行首可有空白，随后是 `$$`，本行存在第二个未转义 `$$`，第二个 `$$` 后只允许空白。
- 多行开始/结束：整行除空白外只能是 `$$`。
- `$$x+1` 没有同行闭合且又不是独立 `$$` 行时，不应切换多行公式状态。
- 未闭合的多行 `$$` 块从开始分隔行保护到文档末尾。

### 2.3 CJK 选区输入 `<` 不转换为书名号

选中含 CJK 的文本后输入 `<`，继续使用既有 ASCII `<>` 包裹，不转换为 `《》`。

需要删除或修正文档/测试中与此相反的旧描述。CJK 全角选区映射只包括：

| 输入 | CJK 选区包裹 |
|---|---|
| `(` | `（选区）` |
| `[` | `【选区】` |
| `"` | `“选区”` |
| `'` | `‘选区’` |
| `<` | `<选区>`，保持 ASCII |

键盘 KeyPress 和 IME commit 必须得到一致结果。

---

## 3. 项目架构与不可破坏的边界

### 3.1 技术栈

ScratchEditor 是 Windows 上的 Qt Quick 编辑器：

- C++20；
- Qt 6.10.2；
- Qt Core、Gui、Network、Qml、Quick；
- MinGW 13.1；
- CMake 3.25+；
- Ninja；
- QML `TextEdit` 作为编辑表面；
- C++ 负责命令、事件过滤、IPC、配置和文档操作。

工作区工具链位于 `.tools/Qt`，不进入 Git。构建 preset 定义在 `CMakePresets.json`。

### 3.2 相关组件职责

```text
qml/Main.qml
  TextEdit editor
      |
      | controller.registerEditor(editor)
      v
EditorController
  - 取得 QQuickTextDocument / QTextDocument
  - 给 TextEdit 安装 eventFilter
  - 将测试 IPC 转换为真实 QKeyEvent / QInputMethodEvent
      |
      | m_commands->handleEditorEvent(event)
      v
EditorCommandRegistry
  - 命令注册和 Alt+F 路由
  - KeyPress / InputMethod 分流
  - 配对、标点、自动空格、选区和 QTextCursor 编辑
      |
      v
QTextDocument + QML TextEdit cursor/selection properties
```

关键位置：

- `qml/Main.qml`：`TextEdit` 是纯文本编辑器，`textFormat: TextEdit.PlainText`。
- `EditorController::registerEditor()`：取得 `QQuickTextDocument::textDocument()` 并调用 `EditorCommandRegistry::setEditor()`。
- `EditorController::eventFilter()`：编辑器事件的总入口。
- `EditorCommandRegistry::handleEditorEvent()`：键盘和 IME 的业务入口。
- `tests/editing_main.cpp`：通过命名管道驱动隔离的 `--test-mode` 实例。

### 3.3 为什么不能依赖 MarkdownHighlighter

修复中的保护区解析必须基于纯 `QString`，不能依赖 `MarkdownHighlighter`：

1. 行内公式当前不由高亮器识别。
2. 自动输入逻辑需要在文本改变前后立即判断。
3. 纯文本解析可以直接做确定性单元测试。
4. 高亮状态可能异步更新，不适合作为编辑语义来源。

高亮器可以继续独立工作，本任务不要求修改 `markdownhighlighter.cpp/.h`。

### 3.4 QML 和控制器修改边界

生产行为修复应集中在：

- 新的纯文本 CJK 处理模块；
- `editorcommandregistry.cpp/.h`；
- `tests/editing_main.cpp`；
- `CMakeLists.txt`。

不需要修改 `qml/Main.qml`。只有为了增加安全的 test-mode 测试入口时，才允许小范围修改 `editorcontroller.cpp`；不得改变生产 IPC、窗口行为或默认管道。

### 3.5 字符位置单位

所有位置必须继续使用 Qt `QString`/`QTextDocument` 的 UTF-16 code-unit 索引：

- `selectionStart`、`selectionEnd`、`cursorPosition`；
- `QString::size()`、`mid()`；
- `QTextCursor::setPosition()`；
- 保护区 span 和插入点。

不得混用 UTF-8 字节偏移。现有 CJK 范围只处理 BMP `QChar`，本修复不扩大到 Extension B 等代理对字符，除非另有需求。

---

## 4. 构建、运行和测试模型

### 4.1 开发阶段必须使用隔离 preset

开发和测试时使用 `editing` preset，并传 `-SkipLocalInstall`：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -Preset editing `
  -SkipLocalInstall
```

原因：不带 `-SkipLocalInstall` 的构建脚本会把程序同步到 `%LOCALAPPDATA%\ScratchEditor\CodexEditor` 和 `%LOCALAPPDATA%\ScratchEditor\AhkEditor`，还可能通过 IPC 重启稳定常驻实例。普通修复迭代不应触碰这些安装副本。

如果当前 PowerShell 策略允许直接运行脚本，也可以使用 `./scripts/build.ps1`；但已知本机可能阻止未签名脚本，因此交接命令统一写成 `powershell.exe -ExecutionPolicy Bypass -File`。

### 4.2 editing 验收程序

`CMakeLists.txt` 当前生成：

- `ScratchEditor.exe`；
- `ScratchEditorEditingTests.exe`；
- 其他窗口、性能和外部编辑器测试程序。

`scripts/run-editing-tests.ps1` 会：

1. 启动 `ScratchEditor.exe --background --test-mode` 隔离实例；
2. 使用独立命名管道和临时 settings INI；
3. 运行 `ScratchEditorEditingTests.exe`；
4. 验证几何和快捷键持久化；
5. 验证原 AHK 文件哈希和仓库状态没有变化；
6. 通过 test-mode `quit` IPC 停止隔离实例；
7. 将 JSON 结果写入 `artifacts/`。

不要执行 `ScratchEditor.exe --quit`。`quit` 是 test-mode JSON IPC 命令，不是命令行参数。

### 4.3 推荐 editing 测试命令

优先设置一个真实、只读的阶段 6 AHK 备份路径：

```powershell
$env:SCRATCHEDITOR_ORIGINAL_AHK = 'D:\path\to\KeysRedirect.ahk.stage6-backup'

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editing-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Editing.CjkFix
```

也可以显式传 `-OriginalAhkPath`。不要把某台机器的绝对 AHK 路径提交到脚本或文档配置。

### 4.4 测试时序注意事项

键盘默认插入和自动空格之间存在 `QTimer::singleShot(0, ...)`。测试客户端与编辑器是两个进程：

- `testKeyPress` 返回时，服务端的零延迟回调可能尚未执行；
- 后续 `testText`/`status` IPC 请求会给服务端事件循环机会执行回调；
- 不要只依赖测试客户端自己的 `QCoreApplication::processEvents()`；
- 每个异步输入断言前应发起一个后续 IPC 请求，必要时保留小幅、有限的等待；
- 不应使用数秒级固定 sleep 掩盖竞态。

### 4.5 当前基线测试事实

- 提交时生成的 `editing-results-20260804-151437.json` 曾全绿。
- 调查期间重新运行全套测试时，新增 CJK 聚合检查均通过，但若干既有检查因 IPC read timeout/窗口拖动时序失败。
- 因此不能把当前已有 CJK 检查通过理解为功能正确；下面列出的定向复现均稳定失败。
- 实施者应先重新构建再跑基线。如果仍出现非 CJK timeout，应单独记录并最多重跑一次，不能删除或放宽业务断言来“变绿”。

---

## 5. 当前实现清单与根因

### 5.1 当前相关函数

`src/editorcommandregistry.cpp` 中已有：

- `isInsideFencedCode()`；
- `getInlineSpansOnLine()`；
- `EditorCommandRegistry::isInsideBlockFormula()`；
- `isCJK()`、`isAsciiAlnum()`、`isSoftSeparator()`；
- `formatLineSpacing()`；
- `handleEditorEvent()`；
- `handleTypedText()`；
- `completeInputMethodCommit()`；
- `formatSpacing()`、`formatSpacingInRange()`；
- `autoSpaceAroundCursor()`、`autoSpaceAroundRange()`。

这些逻辑目前分散在事件处理、匿名命名空间和公开 static 方法中，且实时输入与手动整理使用不同算法。这是缺陷重复出现的主要结构性原因。

### 5.2 已确认缺陷和复现

#### BUG-01：行内代码内部被自动插空格

```text
初始：`中文`
光标：中|文
动作：键盘输入 A
实际：`中 A 文`
期望：`中A文`
```

`handleTypedText()` 和 `autoSpaceAroundRange()` 只检查围栏代码/块公式，不检查行内 span。

#### BUG-02：行内公式内部被自动插空格

```text
初始：$中文$
光标：中|文
动作：键盘输入 A
实际：$中 A 文$
期望：$中A文$
```

#### BUG-03：实时输入没有补行内 span 外围空格

```text
动作：依次输入 中文、反引号配对、A、跳出闭合反引号、文
实际：中文`A`文
期望：中文 `A` 文
```

需求文档写了外围空格，但实时算法只处理直接相邻的 CJK/ASCII 字符。

#### BUG-04：Alt+F 局部选区破坏行内代码

```text
初始：`中文ABC`
选区：只选定中文ABC，不含反引号
动作：Alt+F / execute("formatSpacing")
实际：`中文 ABC`
期望：`中文ABC`
```

根因是 `formatSpacingInRange()` 先截取 `targetSegment`，再对片段调用 `formatLineSpacing()`；片段失去了外部反引号上下文。

#### BUG-05：Alt+F 局部选区破坏行内公式

```text
初始：$中文ABC$
选区：只选定中文ABC，不含美元符号
实际：$中文 ABC$
期望：$中文ABC$
```

#### BUG-06：围栏中的公式标记污染后续状态

````text
```
$$
```
中文A
````

在最后输入 `A` 时，实际保持 `中文A`，期望 `中文 A`。公式扫描器不知道 `$$` 位于代码围栏中，因此错误进入公式状态。

#### BUG-07：公式中的围栏标记污染后续状态

````text
$$
```
$$
中文A
````

围栏扫描器不知道反引号位于公式块中，公式结束后仍错误认为处于代码围栏。

#### BUG-08：IME CJK 选区包裹不使用全角映射

```text
初始：中文，全选
动作：IME commit "("
实际：(中文)
期望：（中文）
```

全角映射只在 `handleTypedText()` 中，`completeInputMethodCommit()` 直接复用 ASCII pair。

#### BUG-09：带尾随文本的 fence 被错误视为关闭 fence

````text
```js
中文ABC
```oops
后续ABC
```
````

执行 Alt+F 后，实际把 `后续ABC` 改成 `后续 ABC`。` ```oops ` 不能关闭 fence；关闭 fence 在 run 之后只允许空白。

#### BUG-10：单行块公式吞掉后续文档

```text
$$x+1$$
中文ABC
```

当前 Alt+F 保持第二行 `中文ABC`，因为第一行错误切换为“公式块内部”。正确结果是：

```text
$$x+1$$
中文 ABC
```

#### BUG-11：Alt+F 后选区折叠

```text
初始：前中文ABC后
选区：[1, 6) == 中文ABC
整理后文本：前中文 ABC后
实际选区：[7, 7)
期望选区：[1, 7)，继续选中整理后的文本
```

#### BUG-12：保护区内标点仍被改写

以下场景都已复现：

```text
`中|文`       输入 ,  -> `中，文`
$中|文$       输入 ,  -> $中，文$
围栏中的中|文 输入 ,  -> 中，文
公式块中中|文 输入 ,  -> 中，文
```

按已确认决策，这些实际结果都错误，应插入原始半角逗号。

### 5.3 性能风险

当前 `autoSpaceAroundRange()` 在循环的每个候选位置：

1. 调用 `m_document->toPlainText()` 复制全文；
2. 从文档开头扫描围栏状态；
3. 再从文档开头扫描公式状态。

多字符 IME 提交长度为 K、文档长度为 N 时，最坏行为接近 O(K×N)，并伴随重复内存分配。修复必须把每次操作改为：

- 最多读取一次全文；
- 最多分析一次保护区；
- 候选边界线性处理；
- 插入位置一次性收集后倒序写入。

---

## 6. 修复后的规范行为

### 6.1 CJK 与 ASCII 分类

保持基线提交的范围，不在本修复中扩张：

- CJK Unified Ideographs：`0x4E00-0x9FFF`；
- Extension A：`0x3400-0x4DBF`；
- Hiragana：`0x3040-0x309F`；
- Katakana：`0x30A0-0x30FF`；
- Katakana Phonetic Extensions：`0x31F0-0x31FF`；
- Hangul Jamo：`0x1100-0x11FF`；
- Hangul Compatibility Jamo：`0x3130-0x318F`；
- Hangul Syllables：`0xAC00-0xD7AF`。

ASCII alnum 仅包括 `A-Z`、`a-z`、`0-9`。

### 6.2 标点转换

只对无 Ctrl/Alt/Meta 的直接键盘输入生效；IME commit 和粘贴不执行半角标点转全角。

保护区外保留现有规则：

- CJK 后 `, . : ? ! ; )` 转对应全角；
- CJK 后 `(` 插入 `（）` pair；
- `，,` 转 `，，`；
- `。.` 转 `。。`；
- 三个 `.`/`。` 的允许混合组合转 `……`；
- CJK 后 `--` 转 `——`。

保护区内部上述规则全部关闭，字符按原始输入交给既有编辑流。注意：关闭 CJK 转换不等于删除既有 ASCII delimiter pair completion。

### 6.3 自动空格

保护区外的相邻字符满足下列任一条件时插入一个 ASCII space：

- CJK + ASCII alnum；
- ASCII alnum + CJK。

下列情况不插入：

- 已经有空格；
- 中间是换行；
- 任一侧是既定软分隔符；
- 插入点位于任一保护区内部；
- 操作来自粘贴。

不删除已有空格，不把多个空格归一化，不处理 CJK 与非 ASCII Unicode 字母之间的空格。

### 6.4 行内 span 外围空格

完整、闭合的行内代码或公式被视为一个不可修改单元：

- span 内部永远不插空格；
- span 左外边界：左侧是 CJK/ASCII alnum 且不是软分隔符时插空格；
- span 右外边界：右侧是 CJK/ASCII alnum 且不是软分隔符时插空格；
- 已有空格时不重复；
- 未闭合的反引号或 `$` 不构成受保护 span。

示例：

```text
中文`code`中文     -> 中文 `code` 中文
中文，`code`       -> 不变
`code`，中文       -> 不变
中文(`code`)文本   -> 不变
测试`中文ABC`结束  -> 测试 `中文ABC` 结束
```

### 6.5 Alt+F 的范围语义

- 有选区：只允许在选区内部的边界插入空格；不得越过选区起止位置修改外部文本。
- 无选区：只整理当前逻辑行，不包含换行符。
- 即使选区只覆盖保护区内容的一部分，也必须根据全文绝对位置识别保护区。
- 整理后保持选区方向和逻辑范围；新增空格位于选区内部时，选区终点相应增长。
- 无选区时保持光标的逻辑位置。
- 所有本次整理必须由一次 Undo 完整撤销。

### 6.6 键盘与 IME 一致性

- CJK 选区的 `(`、`[`、`"`、`'` 包裹在 KeyPress 和 IME commit 中一致。
- `<` 在两条路径都保持 ASCII `<>`。
- IME 提交普通多字符文本时检查提交片段左右两端。
- IME 在行内代码/公式内部提交时不得整理内部内容。
- IME 提交的半角标点不做 CJK 标点二次转换，维持既有 Q4 决策。

### 6.7 粘贴

Ctrl+V 或系统粘贴不得触发实时自动空格/标点转换。用户可在粘贴后显式执行 Alt+F。

---

## 7. 推荐目标代码结构

### 7.1 新建纯文本模块

推荐新增：

```text
src/cjktextprocessor.h
src/cjktextprocessor.cpp
```

该模块只能依赖 Qt Core 字符串/容器，不依赖 QObject、QTextDocument、QQuickItem、EditorController 或 MarkdownHighlighter。

建议命名空间和数据模型：

```cpp
namespace CjkText {

enum class ProtectedKind {
    FencedCode,
    BlockFormula,
    InlineCode,
    InlineFormula,
};

struct ProtectedSpan {
    int outerStart = 0;   // opening delimiter 的首位置
    int outerEnd = 0;     // closing delimiter 后的 exclusive 位置
    int contentStart = 0;
    int contentEnd = 0;
    ProtectedKind kind{};
};

struct DocumentAnalysis {
    QVector<ProtectedSpan> blockSpans;  // 整块禁止编辑
    QVector<ProtectedSpan> inlineSpans; // 内部禁止，outerStart/outerEnd 可作为外围空格边界
};

struct BoundaryRange {
    int first = 1; // 字符间插入位置，inclusive
    int last = 0;  // inclusive；first > last 表示空
};

bool isCjk(QChar ch);
bool isAsciiAlnum(QChar ch);
bool isSoftSeparator(QChar ch);
DocumentAnalysis analyzeDocument(const QString &text);
QVector<int> collectSpacingInsertions(
    const QString &text,
    BoundaryRange allowedBoundaries,
    const DocumentAnalysis &analysis);

} // namespace CjkText
```

名称可按仓库风格微调，但必须保持“纯函数 + 绝对坐标 + 可直接测试”的结构。

### 7.2 span 坐标约定

统一使用半开区间 `[start, end)`：

- `outerStart` 指向 opening delimiter 第一个字符；
- `outerEnd` 指向 closing delimiter 后一个位置；
- `contentStart/contentEnd` 表示定界符之间的内容；
- block span 包含 opening/closing 行；
- inline span 的 `outerStart` 和 `outerEnd` 是允许考虑外围空格的两个边界；
- `outerStart < pos < outerEnd` 的插入点一律视为 span 内部并拒绝。

span 必须按 `outerStart` 排序且不得无意重叠。解析器状态决定 block 与 inline 的优先级。

### 7.3 单一 block 状态机

逐行扫描全文，状态只能是：

```text
Normal
FencedCode(activeChar, minimumRunLength, openingStart)
BlockFormula(openingStart)
```

规则：

#### Normal 状态

1. 先检查 fence opener；若匹配，进入 FencedCode。
2. 再检查单行 `$$...$$`；若匹配，添加只覆盖本行的 BlockFormula span，不改变状态。
3. 再检查独立 `$$` 行；若匹配，进入 BlockFormula。
4. 普通行才解析 inline code/formula span。

#### FencedCode 状态

- 只识别同字符、run 长度不短于 opening、run 后只有空白的 closing fence。
- `$$`、另一种 fence 字符、较短 run、` ```oops ` 都只是代码内容。
- closing 后生成从 opening 行开始到 closing 行结束的 block span，并回到 Normal。
- EOF 未闭合时，span 延伸到文档末尾。

#### BlockFormula 状态

- 只识别独立 `$$` closing 行。
- fence-looking 行和单行公式-looking 文本都只是公式内容。
- closing 后生成完整 block span并回到 Normal。
- EOF 未闭合时，span 延伸到文档末尾。

这种互斥状态机直接修复 BUG-06/07。

### 7.4 fence 解析

为了保持当前兼容性，可以继续允许任意行首空格/Tab；本修复的强制要求是 opener 和 closer 分开判断：

- opener：`^[\t ]*(`{3,}|~{3,})[^\n]*$`；
- closer：动态检查相同字符、长度不少于 opening，并且 run 后只能是空格/Tab。

不要继续用同一个允许任意尾随文本的正则同时识别 opener 和 closer。

至少测试：

- ` ``` ` 正常关闭；
- opening 4 个反引号不能被 3 个关闭；
- opening 3 个可以被 4 个关闭；
- 反引号不能被波浪线关闭；
- ` ```oops ` 不能关闭；
- ` ```   ` 可以关闭。

### 7.5 inline 解析

逐普通行解析：

#### Inline code

- opening 是完整 backtick run；
- closing 必须是长度完全相同的完整 run，不能从更长 run 中截取子串；
- span 不跨换行；
- 未闭合 run 不产生 span。

#### Inline formula

- 只处理单个、未转义 `$` opening/closing；
- `$$` 留给 block formula 规则，不作为两个 inline delimiter；
- span 不跨换行；
- 未闭合 `$` 不产生 span；
- 至少支持反斜杠转义判断，避免 `\$` 被误识别。

实现时不要为每个字符遍历所有 span。解析和边界规划都应使用排序 span + 单调索引，保持线性复杂度。

### 7.6 统一空格插入规划

`collectSpacingInsertions()` 只返回原文绝对坐标，不直接修改文本。

对每个允许的字符间边界 `pos`：

1. 验证 `1 <= pos < text.size()`。
2. 如果边界位于 block span 内，跳过。
3. 如果存在 inline span：
   - `span.outerStart < pos < span.outerEnd`：跳过；
   - `pos == span.outerStart`：只评估 span 左外侧；
   - `pos == span.outerEnd`：只评估 span 右外侧。
4. 已有空格或换行则跳过。
5. 普通边界按 CJK/ASCII 规则判断。
6. inline 外边界按“外侧字符是 CJK/ASCII alnum，且不是软分隔符”判断。
7. 去重并按升序返回。

应用时按坐标倒序插入，防止前面的插入改变后续坐标。

### 7.7 全文上下文与局部授权范围分离

这是修复局部选区 bug 的关键：

- 保护区分析永远针对完整 `documentText`；
- 允许修改的 boundary range 才由选区/当前行/本次输入决定；
- 不要先截取 selection 再解析；
- 不要给 span 使用相对选区坐标。

手动选区 `[rangeStart, rangeEnd)` 只允许：

```text
firstBoundary = rangeStart + 1
lastBoundary  = rangeEnd - 1
```

因此不会在选区边缘之外插入空格。当前行采用同一规则。

### 7.8 应用编辑和位置映射

建议新增通用位置映射：

```cpp
int positionAfterInsertions(int originalPosition,
                            const QVector<int> &insertions,
                            bool includeInsertionAtPosition);
```

手动 Alt+F 前保存：

- `selectionStart`；
- `selectionEnd`；
- `cursorPosition`，用于判断选区方向和 active end。

插入后：

- collapsed selection：恢复映射后的 cursor；
- forward selection：恢复映射后的 start/end；
- reverse selection：保持 active end 在原方向；
- 选区内部新增的空格必须被新选区包含。

所有 Alt+F 插入必须位于一个 `beginEditBlock()/endEditBlock()` 中。选区恢复不应产生额外 undo step。

---

## 8. EditorCommandRegistry 集成方案

### 8.1 header 清理

`editorcommandregistry.h` 当前把多个 CJK helper 暴露为 public static。完成纯模块后：

- 将 `isCJK()`、`isAsciiAlnum()`、`isSoftSeparator()` 移入纯模块；
- 移除或私有化不再需要的 wrapper；
- 删除 `isInsideBlockFormula()` 和 `formatLineSpacing()` 的旧公开入口；
- 用新的私有方法表达“分析、计划、应用”，不要继续堆叠相似 helper。

修改前先用 `rg` 确认没有仓库外显式 API 依赖；它们不是 Q_INVOKABLE，当前只应被本实现使用。

### 8.2 输入变更 footprint

实时空格必须围绕“本次编辑影响的边界”，不能扫描并整理整行旧内容。建议定义：

```cpp
struct EditFootprint {
    int start = 0; // post-edit 文本中变更片段开始
    int end = 0;   // post-edit exclusive
};
```

候选边界通常为：

- `start`：新片段左侧边界；
- `end`：新片段右侧边界。

这恰好支持：

- CJK 后输入 ASCII；
- ASCII 前插入 CJK；
- 多字符 IME 两端；
- 插入完整 inline pair 后检查 pair 左右外围；
- 在 inline 内容内部输入时，两端均被 span 规则拒绝。

### 8.3 handleTypedText 返回更多语义

当前 bool 只能表示事件是否消费，无法描述是否改变文本和需要检查哪些边界。建议改为内部结果类型：

```cpp
struct TypedEditResult {
    bool consumed = false;
    bool textChanged = false;
    bool runAutoSpacing = false;
    EditFootprint footprint;
};
```

典型结果：

- 跳过已有 closing delimiter：`consumed=true, textChanged=false`；
- CJK 标点转换：`consumed=true, textChanged=true, runAutoSpacing=false`；
- 插入 pair/包裹选区：`consumed=true, textChanged=true, runAutoSpacing=true`；
- 普通文本交给 Qt：`consumed=false`，由 queued callback 在默认插入后运行。

如果不采用该结构，必须提供同等明确的 post-edit footprint；不能继续只读取回调执行时的“当前光标”猜测编辑位置。

### 8.4 保护区内标点短路

在 `handleTypedText()` 的 CJK 标点/省略号/破折号分支前：

1. 对编辑前全文执行一次 `analyzeDocument()`；
2. 根据 collapsed cursor 判断是否位于保护区内部；
3. 若在保护区内，跳过所有本次 CJK 自动转换；
4. 继续进入既有普通 delimiter/default insertion 流程。

不要简单 `return false` 后绕过既有 pair completion，也不要让这项改动影响 Markdown 列表或 Tab 行为。

### 8.5 删除重复的即时 CJK–ASCII 插入逻辑

当前 `handleTypedText()` 末尾有一套即时 CJK/ASCII 插入，`handleEditorEvent()` 又安排异步 `autoSpaceAroundRange()`。两套逻辑必须合并，否则保护和坐标仍会分叉。

推荐：

- 普通 KeyPress 让 Qt 完成默认插入；
- queued callback 使用捕获的 pre-edit selection 和实际 post-edit 文本构造 footprint；
- 调用统一 planner；
- pair 等本函数已处理的插入使用明确 footprint 同步或安全排队调用同一 planner。

### 8.6 queued callback 安全

所有零延迟回调必须：

- 以 `this` 为 receiver，使 registry 销毁后自动取消；
- 回调内再次检查 `m_editor` 和 `m_document`；
- 如捕获原 editor/document，使用 `QPointer` 验证仍是同一对象；
- 不在 `completeInputMethodCommit()` 早退后无条件解引用 `m_editor`；
- 使用捕获的编辑起点/长度，不使用可能已经移动的当前光标推断原编辑范围。

### 8.7 IME 包裹映射

抽出唯一 pair 解析函数，例如：

```cpp
ResolvedPair resolvePairForSelection(const QString &opening,
                                     const QString &selection);
```

规则：

1. 先取得既有 delimiter pair。
2. selection 含 CJK 时应用四项 half-to-full 映射。
3. `<` 不在映射表中。
4. 无 CJK 时保持 ASCII pair。
5. 键盘 `handleTypedText()` 和 IME `completeInputMethodCommit()` 必须调用同一个函数。

`completeInputMethodCommit()` 建议返回 `std::optional<EditFootprint>`，调用者据此决定是否运行自动空格。

### 8.8 Alt+F 重写

用以下流程替换当前按片段 `formatLineSpacing()` 的实现：

```text
documentText = document->toPlainText()                 // 一次
analysis = analyzeDocument(documentText)              // 一次
editableRange = selection 或 current line
allowedBoundaries = editableRange 内部边界
insertions = collectSpacingInsertions(...)
snapshot selection/cursor
beginEditBlock
按倒序插入所有空格
endEditBlock
映射并恢复 selection/cursor
focusEditor
```

如果没有插入：

- 文本、撤销栈、选区和光标均不应变化；
- 不需要创建空 edit block。

### 8.9 不要顺带修改的行为

本任务不是编辑器全面重写。以下功能只做回归，不做语义调整：

- Markdown bold/italic/code toggle；
- 列表 continuation、空列表 Enter/Backspace；
- heading；
- selection drag；
- find/replace；
- 窗口、主题、设置、外部文件；
- 现有 ASCII `)` pair 跳出细节，除非新增测试暴露明确回归。

---

## 9. CMake 与文件修改计划

### 9.1 `src/cjktextprocessor.h/.cpp`（新增）

实现：

- 字符分类；
- block/inline 解析；
- 保护查询；
- spacing insertion planning；
- 必要的纯文本 apply helper，仅供测试时也可保留。

模块中不得出现编辑器对象或异步逻辑。

### 9.2 `CMakeLists.txt`

推荐增加静态库，使生产程序和测试复用同一实现：

```cmake
add_library(ScratchEditorCjkText STATIC
    src/cjktextprocessor.cpp
    src/cjktextprocessor.h
)

target_include_directories(ScratchEditorCjkText PUBLIC src)
target_link_libraries(ScratchEditorCjkText PUBLIC Qt6::Core)

target_link_libraries(ScratchEditor PRIVATE ScratchEditorCjkText)
target_link_libraries(ScratchEditorEditingTests PRIVATE ScratchEditorCjkText)
```

如果 target 声明顺序不允许在原位置链接，应调整顺序，但不要复制编译两套不同源文件。

### 9.3 `src/editorcommandregistry.cpp/.h`

实施第 8 节的集成，删除被新模块替代的旧 parser/formatter。重点避免：

- 同时保留旧 `getInlineSpansOnLine()` 和新 parser；
- 实时与手动继续调用不同分类函数；
- protected region 判断仍各自扫描；
- `toPlainText()` 留在 boundary 循环内部。

### 9.4 `tests/editing_main.cpp`

同时添加两类测试：

1. 直接调用 `CjkText` 纯函数的 parser/planner 测试；
2. 通过 IPC 驱动真实编辑器的 KeyPress、IME、Alt+F、selection、Undo 测试。

不要继续把十几个场景聚合成一个 bool 且不给实际值。每个关键边界应有独立 check 名和包含 `actual/expected` 的 details，便于一次定位。

### 9.5 `src/editorcontroller.cpp`（仅测试入口，可选）

如果要自动验证 Ctrl+V，允许新增仅在现有 test-mode dispatch 中可达的安全粘贴命令，但必须：

- 不暴露到生产 IPC；
- 保存并完整恢复剪贴板 MIME 内容，不能只恢复纯文本；
- 即使测试异常也在 finally/RAII 中恢复；
- 不影响现有 `clipboardHealthy` 断言。

如果无法保证无损恢复系统剪贴板，则不要增加危险测试命令；保留人工 Ctrl+V 验收，并用 modifier 路由的单元/代码审查证明不会调度自动整理。

### 9.6 文档

- 本文是修复权威计划。
- `docs/cjk-features-plan.md` 保留为原始实施历史，但修正 `<` 测试清单的直接矛盾，或在开头注明由本文覆盖。
- `docs/README.md` 增加本文链接。

---

## 10. 自动测试详细矩阵

### 10.1 纯 parser 测试

| ID | 输入/条件 | 必须断言 |
|---|---|---|
| PARSE-001 | 普通 ` ```js ... ``` ` | 一个完整 FencedCode span |
| PARSE-002 | ` ```oops ` 位于 fence 内 | 不关闭 fence |
| PARSE-003 | 4 backticks 开、3 backticks 行 | 不关闭 |
| PARSE-004 | 3 backticks 开、4 backticks 关 | 正常关闭 |
| PARSE-005 | backtick 开、tilde 行 | 不关闭 |
| PARSE-006 | fence 内包含 `$$` | 只生成 fence block，不改变 formula state |
| PARSE-007 | formula block 内包含 ``` | 只生成 formula block，不改变 fence state |
| PARSE-008 | `$$x+1$$` | 单行 BlockFormula span，后续行 Normal |
| PARSE-009 | `$$ x $$` | 单行 BlockFormula span |
| PARSE-010 | 独立 `$$` 开关 | 多行 BlockFormula span |
| PARSE-011 | 未闭合独立 `$$` | span 延伸到 EOF |
| PARSE-012 | `$$x+1` | 不切换多行公式状态 |
| PARSE-013 | `` `code` `` | InlineCode span 的 outer/content 坐标准确 |
| PARSE-014 | 双 backtick code span | exact run 正确配对 |
| PARSE-015 | 单 backtick opener + 双 run | 不能从更长 run 中截取错误 closing |
| PARSE-016 | 未闭合 backtick | 不产生 span |
| PARSE-017 | `$x+1$` | InlineFormula span |
| PARSE-018 | `\$x$` | escaped opening 不产生错误 span |
| PARSE-019 | 行内 span 位于 fence/formula 中 | 不额外生成 inline span，block 优先 |

所有测试要断言绝对坐标，不只断言 span 数量。

### 10.2 纯 spacing planner 测试

| ID | 输入 | 授权范围 | 期望 |
|---|---|---|---|
| SPACE-001 | `中文ABC` | 全行 | `中文 ABC` |
| SPACE-002 | `ABC中文` | 全行 | `ABC 中文` |
| SPACE-003 | `中文123` | 全行 | `中文 123` |
| SPACE-004 | `Python3` | 全行 | 不变 |
| SPACE-005 | `中文 ABC` | 全行 | 不重复空格 |
| SPACE-006 | `中文，ABC` | 全行 | 不变 |
| SPACE-007 | `中文-ABC` | 全行 | 不变 |
| SPACE-008 | `ABC/中文` | 全行 | 不变 |
| SPACE-009 | ``中文`code`中文`` | 全行 | ``中文 `code` 中文`` |
| SPACE-010 | `公式$x+1$成立` | 全行 | `公式 $x+1$ 成立` |
| SPACE-011 | `` `中文ABC` `` | 全行 | span 内容不变 |
| SPACE-012 | `$中文ABC$` | 全行 | span 内容不变 |
| SPACE-013 | 只授权 inline 内容内部边界 | 局部 | 无 insertion |
| SPACE-014 | range 截断一侧 delimiter | 局部 | 仍依全文 span 拒绝内部 insertion |
| SPACE-015 | fence 内 `中文ABC` | 全文 | 无 insertion |
| SPACE-016 | fence 后 `中文ABC` | 全文 | 后续普通行有 insertion |
| SPACE-017 | 单行 `$$x$$` 后 `中文ABC` | 全文 | 只整理后续行 |
| SPACE-018 | 空范围/单字符范围 | 局部 | 无 insertion、无越界 |

### 10.3 KeyPress 集成测试

标点转换每个场景单独命名：

| ID | 初始/动作 | 期望 |
|---|---|---|
| KEY-001 | `中文` 后输入 `,` | `中文，` |
| KEY-002 | 后输入 `.` | `中文。` |
| KEY-003 | `:` | `中文：` |
| KEY-004 | `?` | `中文？` |
| KEY-005 | `!` | `中文！` |
| KEY-006 | `;` | `中文；` |
| KEY-007 | `(` | `中文（）`，光标在 pair 中 |
| KEY-008 | `，` 后输入 `,` | `，，` |
| KEY-009 | `。` 后输入 `.` | `。。` |
| KEY-010 | `...` | `……` |
| KEY-011 | `。。。` | `……` |
| KEY-012 | `。。.`、`.。.` 等允许混合 | `……` |
| KEY-013 | `中文--` | `中文——` |

保护测试必须覆盖四类区域和至少三种转换：

| ID | 场景 | 期望 |
|---|---|---|
| PROTECT-KEY-001 | inline code 内 CJK 后输入 `,` | 保留 `,` |
| PROTECT-KEY-002 | inline formula 内输入第三个 `.` | 不合并省略号 |
| PROTECT-KEY-003 | fence 内 CJK 后输入第二个 `-` | 不转破折号 |
| PROTECT-KEY-004 | block formula 内 CJK 后输入 `,` | 保留 `,` |
| PROTECT-KEY-005 | 单行 `$$...$$` 内输入 | 不自动改写 |

实时空格：

| ID | 初始/动作 | 期望 |
|---|---|---|
| LIVE-001 | `中文` 后键入 `A` | `中文 A` |
| LIVE-002 | `ABC` 后 IME/键入 CJK | `ABC 中文` |
| LIVE-003 | `中|文` 插入 `A` | `中 A 文`，光标位于新增片段之后且在右空格之前/之后符合编辑语义 |
| LIVE-004 | `` `中|文` `` 输入 `A` | `` `中A文` `` |
| LIVE-005 | `$中|文$` 输入 `A` | `$中A文$` |
| LIVE-006 | 完整输入 ``中文`A`文`` | ``中文 `A` 文`` |
| LIVE-007 | 完整输入 `中文$x$文` | `中文 $x$ 文` |
| LIVE-008 | fence 含 `$$`，结束后输入 `中文A` | `中文 A` |
| LIVE-009 | formula 含 ```，结束后输入 `中文A` | `中文 A` |

### 10.4 CJK 选区包裹测试

以下每项都要分别走 KeyPress 和 `testInputMethodCommit`：

| ID | 选区 | 输入 | 期望 |
|---|---|---|---|
| WRAP-001 | `中文` | `(` | `（中文）` |
| WRAP-002 | `中文` | `[` | `【中文】` |
| WRAP-003 | `中文` | `"` | `“中文”` |
| WRAP-004 | `中文` | `'` | `‘中文’` |
| WRAP-005 | `中文` | `<` | `<中文>` |
| WRAP-006 | `ABC` | `(` | `(ABC)` |
| WRAP-007 | `A中文B` | `[` | `【A中文B】` |

断言文本之外，还要断言包裹后内部 selection 仍选中原内容。

### 10.5 Alt+F 集成测试

| ID | 场景 | 断言 |
|---|---|---|
| FORMAT-001 | 无选区，光标在普通行 | 只整理当前行 |
| FORMAT-002 | 多行文档中选择一行片段 | 只修改选区内部 |
| FORMAT-003 | 选区从 inline content 内开始/结束 | 内容不变 |
| FORMAT-004 | 选区跨 inline 左 delimiter | span 内容不变，合法外围可整理 |
| FORMAT-005 | 选区跨 inline 右 delimiter | 同上 |
| FORMAT-006 | 选区覆盖完整 inline span | 外围空格正确、内部不变 |
| FORMAT-007 | fence 内选区 | 不变 |
| FORMAT-008 | block formula 内选区 | 不变 |
| FORMAT-009 | 单行 block formula | 本行不变、下一行可整理 |
| FORMAT-010 | ` ```oops ` 位于 fence | 后续仍受同一 fence 保护 |
| FORMAT-011 | forward selection | 整理后范围扩展且方向不变 |
| FORMAT-012 | reverse selection | active end 方向保持 |
| FORMAT-013 | collapsed cursor，插入发生在光标前 | cursor 按 delta 前移 |
| FORMAT-014 | 无需修改 | 文本、selection、cursor、undo stack 不变 |
| FORMAT-015 | 多处插入 | 一次 Undo 恢复全部，Redo 再次得到整理结果 |

### 10.6 IME 测试

| ID | 场景 | 期望 |
|---|---|---|
| IME-001 | CJK 后提交多字符 `ABC` | 左边界补空格 |
| IME-002 | `中|文` 提交 `ABC` | 两端补空格 |
| IME-003 | inline code 内提交 `ABC` | 内部不改写 |
| IME-004 | inline formula 内提交 `ABC` | 内部不改写 |
| IME-005 | fence/formula block 内提交 | 不改写 |
| IME-006 | CJK 选区提交四种 halfwidth opener | 与键盘一致 |
| IME-007 | 提交 `,` | 不做键盘专属全角转换 |
| IME-008 | 光标和 selection 在异步完成后准确 | status 精确断言 |

### 10.7 粘贴测试

自动化安全可行时：

1. 设置隔离测试剪贴板为 `中文ABC`；
2. 发送真实 Ctrl+V；
3. 断言仍为 `中文ABC`；
4. 执行 Alt+F 后变为 `中文 ABC`；
5. 无损恢复原剪贴板。

若无法无损恢复 MIME 数据，则列为人工强制验收，不得用 `testSetText` 冒充真实粘贴测试。

### 10.8 回归测试

现有以下 checks 必须保持：

- delimiter completion、Tab 跳出、空对退格；
- Markdown bold/italic/code toggle；
- heading/list/task/quote；
- find/replace；
- configurable shortcuts；
- selection drag；
- markdown highlighter/style；
- previewExcluded；
- source/AHK protection。

如果非 CJK 测试出现 timeout，优先调查服务端启动和请求时序，不要改业务期望。

---

## 11. 性能验证

### 11.1 结构性要求

代码审查必须确认：

- `toPlainText()` 不在逐 boundary 循环中；
- `analyzeDocument()` 每次编辑最多调用一次；
- block scanner 单次线性遍历；
- inline scanner 对每行单次遍历；
- spacing planner 不为每个字符遍历全部 spans；
- 文档写入按收集后的坐标倒序执行。

目标复杂度：

```text
分析：O(N)
候选规划：O(B + S)
应用：O(I) 次 QTextCursor 插入
```

其中 N 为文档长度，B 为候选边界数，S 为相关 span 数，I 为插入数。

### 11.2 建议基准

在 release/editing 构建中生成至少 100,000 UTF-16 code units 的文档，包含：

- 普通 CJK/ASCII 行；
- 100 个 inline spans；
- 多个 fence/formula blocks；
- fence 内的 `$$` 和 formula 内的 fence-like 行。

分别测量：

1. `analyzeDocument()`；
2. 全文 `collectSpacingInsertions()`；
3. 100 字符 IME footprint 的实时规划。

至少记录耗时到测试 details/artifact。不要一开始设置过窄、依赖机器的毫秒阈值；首先验证 5 倍输入规模的耗时不会接近 25 倍，以排除二次退化。若需要 gating，可在本机基线后设置宽松上限。

---

## 12. 推荐实施顺序

### 阶段 A：建立基线

1. `git status --short`，确认并记录用户已有改动；不得覆盖无关改动。
2. `git rev-parse HEAD`，确认计划基线或记录后续 commit 差异。
3. 构建 `editing -SkipLocalInstall`。
4. 运行 editing suite，保存 artifact 路径和失败项。
5. 不修改源码，通过现有 IPC 再复现 BUG-01 至 BUG-12 中的代表场景。

### 阶段 B：先补测试

1. 在 `editing_main.cpp` 加入独立命名的失败测试。
2. 优先加入现有接口即可表达的场景：inline 内容、局部选区、IME wrap、嵌套状态、非法 fence、单行公式、selection/undo。
3. 确认测试在旧代码上按预期失败，而不是测试本身超时。
4. 暂不放宽原有 checks。

### 阶段 C：实现纯文本模块

1. 新建 `cjktextprocessor`。
2. 实现分类和 block 状态机。
3. 实现 inline span parser。
4. 实现绝对坐标 spacing planner。
5. 在 editing test executable 中直接测试纯函数。
6. 此阶段不接入 UI 前先让纯 parser/planner tests 全绿。

### 阶段 D：接入手动 Alt+F

1. 改为全文分析 + range 授权。
2. 倒序插入。
3. 恢复 selection/cursor。
4. 验证一次 Undo/Redo。
5. 删除旧 `formatLineSpacing()` 路径，避免双实现。

先接 Alt+F 的原因：它是同步路径，便于验证 parser 和坐标规划，不涉及 Qt 默认插入时序。

### 阶段 E：接入实时 KeyPress/IME

1. 引入 EditFootprint/TypedEditResult 或等价机制。
2. 删除旧即时 spacing 重复逻辑。
3. 接入普通 KeyPress 默认插入后的 planner。
4. 接入 pair/wrap 的明确 footprint。
5. 接入普通和 relevant IME commit。
6. 统一 CJK selection pair resolver。
7. 在 CJK 标点转换前加入保护判断。
8. 验证 queued callback 的对象寿命和位置捕获。

### 阶段 F：完整回归和文档

1. 重跑纯逻辑和 editing integration。
2. 执行性能结构检查和建议基准。
3. 运行下文的完整验证命令。
4. 修正文档 `<` 冲突和旧实现状态。
5. 更新 `docs/README.md` 索引。
6. 检查 `git diff --check` 和最终 `git status`。

---

## 13. 验证命令

### 13.1 必须执行

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -Preset editing `
  -SkipLocalInstall

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editing-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Editing.CjkFix
```

如果需要显式 AHK 基线：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editing-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Editing.CjkFix `
  -OriginalAhkPath D:\path\to\verified\KeysRedirect.ahk.stage6-backup
```

### 13.2 建议完整回归

先构建隔离 release，不安装：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -Preset release `
  -SkipLocalInstall
```

然后按仓库实际可用基线执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-stage2-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-stage1-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Validation.CjkPerf `
  -ArtifactPrefix cjk-fix-performance
```

如果脚本要求特定 preset 或 AHK 参数，以脚本参数和根 README 为准，不要为了运行而改用户环境。

窗口 UI、外部编辑器和阶段 6 脚本不是每次局部迭代的必要项，但发布前应按 `tests/README.md` 的推荐顺序执行完整回归。

### 13.3 静态检查

```powershell
git diff --check
git status --short
rg -n "getInlineSpansOnLine|formatLineSpacing|isInsideBlockFormula|autoSpaceAroundRange" src tests
```

最后一条用于确认旧重复实现是否真正删除或有意保留。不要只因为名字变化就忽略重复逻辑。

---

## 14. 人工验收清单

使用 `build/editing/ScratchEditor.exe --background --test-mode` 以外，还应在隔离可见实例或安全环境人工验证：

1. 微软拼音输入 `中文ABC`，得到 `中文 ABC`。
2. 在 `` `中文ABC` `` 内输入、删除、移动光标，内容不被加空格或改标点。
3. 输入完整 ``中文`code`中文``，两侧自动出现空格。
4. 在 `$中文ABC$` 内输入保持原样，外围按规则加空格。
5. 围栏代码内输入 `中文ABC,...--` 全部保持字面值。
6. 围栏中放入独立 `$$`，退出围栏后普通文本恢复自动空格。
7. 公式块中放入 ```，退出公式后普通文本恢复自动空格。
8. `$$x+1$$` 后下一行 Alt+F 正常整理。
9. 选中中文，分别用物理键盘和 IME 输入四种 opener，结果一致。
10. 选中中文输入 `<`，得到 `<中文>`。
11. 选中 inline code 内部执行 Alt+F，内部保持不变。
12. Alt+F 后选区仍覆盖整理后的文本；一次 Ctrl+Z 全部恢复。
13. 粘贴 `中文ABC` 不自动整理；Alt+F 后才变为 `中文 ABC`。
14. 快速连续输入时没有光标跳跃、崩溃或对旧位置的延迟修改。

---

## 15. 完成定义

只有同时满足以下条件，任务才算完成：

- [ ] 三项用户决策全部落实。
- [ ] BUG-01 至 BUG-12 有自动测试或明确的人工验收覆盖。
- [ ] 实时输入和 Alt+F 使用同一纯文本 parser/planner。
- [ ] 保护区使用互斥 block 状态机，不再由两个独立 scanner 猜测。
- [ ] 单行 `$$...$$` 不污染后续行。
- [ ] inline 内容在完整、局部选区和实时输入中都不被修改。
- [ ] inline 外围空格在键盘和 IME 流程中生效。
- [ ] KeyPress/IME CJK 选区包裹一致，`<` 保持 ASCII。
- [ ] Alt+F 保持 selection/cursor 并支持一次 Undo。
- [ ] 不在循环中重复 `toPlainText()` 或全文扫描。
- [ ] 新增测试在旧实现上可失败、在新实现上通过。
- [ ] 原 editing 回归通过；任何非 CJK timeout 有独立说明，未通过删除断言掩盖。
- [ ] `git diff --check` 通过。
- [ ] 开发构建未覆盖本机稳定安装副本。
- [ ] 没有修改用户 AHK、剪贴板持久内容或无关配置。

---

## 16. 风险与实施提示

### 16.1 选区方向

QML 暴露的 `selectionStart/selectionEnd` 通常是排序后的范围，真正 active end 由 `cursorPosition` 体现。只恢复 start/end 可能把 reverse selection 变成 forward selection。测试必须包含反向选择。

### 16.2 插入点 affinity

位置恰好等于某个插入点时，cursor/anchor 是否随插入右移要按语义分别决定。手动选区不允许在 range 边缘插入，可显著减少歧义；内部插入应扩展 selection end。

### 16.3 QTextCursor edit block

收集完 insertion 后再开始 edit block。不要在分析期间修改文档。不同 `QTextCursor` 是否加入同一 edit block必须通过 Undo 测试确认，不要只依据代码外观判断。

### 16.4 异步事件竞态

不要让零延迟回调在 editor 被替换、document 变化或新输入已经发生后操作旧坐标。捕获并验证对象和编辑 footprint；必要时检查预期文本或 document revision。

### 16.5 Markdown 兼容

本计划要求修复明确的 fence closing 规则和 exact backtick run，不要求实现完整 CommonMark parser。不要在同一改动中引入复杂嵌套 Markdown 语法，避免扩大回归面。

### 16.6 测试输出可诊断性

现有 CJK tests 把多个场景合并为 `cjkAutoSpacing` 等单个 bool。新测试应使用稳定 ID，并在 details 中输出：

- input；
- action；
- expected text；
- actual text；
- cursor/selection；
- parser spans 或 insertion positions。

这样即使完整 JSON artifact 很大，也能快速定位失败。

---

## 17. 新 session 开始工作时的最短检查清单

1. 完整阅读本文。
2. 阅读根 `README.md` 的“架构与目录”“工具链与构建”“验收”。
3. 阅读 `tests/README.md`。
4. 查看 `git status --short`，保护用户已有改动。
5. 查看 `git log -1 --stat`，确认当前基线。
6. 定位本文列出的 registry 函数和 editing test CJK 区段。
7. 使用 `editing -SkipLocalInstall` 构建。
8. 先补能稳定失败的测试。
9. 实现纯 parser/planner，再接 Alt+F，最后接 KeyPress/IME。
10. 按完成定义验收，汇报实际运行命令、artifact 和剩余风险。

如果仓库在 `f02b22f` 之后已有相关修改，先对照本文逐项确认哪些缺陷已解决；不要机械覆盖新实现，也不要重复已经完成且有测试证据的工作。
