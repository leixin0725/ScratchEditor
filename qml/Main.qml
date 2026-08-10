import QtQuick

Window {
    id: root

    width: 920
    height: 640
    minimumWidth: 500
    minimumHeight: 320
    visible: false
    color: themeBackgroundColor
    title: controller.externalFileMode && controller.externalFileName.length > 0
           ? controller.externalFileName + " — ScratchEditor"
           : "ScratchEditor"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    readonly property int dragZoneHeight: 52
    readonly property int marginSize: 18
    readonly property int resizeMargin: 8
    readonly property int edgeDragWidth: marginSize - resizeMargin
    readonly property bool cornerResizeEnabled: true
    readonly property bool edgeDragEnabled: true
    readonly property bool verticalScrollBarVisible: scrollThumb.visible
    readonly property bool commandPaletteLoaded: commandPaletteLoader.active
    readonly property bool findPanelVisible: findPanel.visible
    readonly property bool settingsPageLoaded: settingsLoader.active
    readonly property bool settingsPageVisible: settingsLoader.active
    readonly property bool darkTheme: controller.theme !== "light"
    readonly property color themeBackgroundColor: darkTheme ? "#252525" : "#f7f8fa"
    readonly property color themeEditorSurfaceColor: darkTheme ? "#292929" : "#ffffff"
    readonly property color themeHeaderColor: themeBackgroundColor
    readonly property color themePanelColor: darkTheme ? "#292929" : "#ffffff"
    readonly property color themeFieldColor: darkTheme ? "#1d1d1d" : "#f5f7fa"
    readonly property color themeTextColor: darkTheme ? "#f2f2f2" : "#24292f"
    readonly property color themeStrongTextColor: darkTheme ? "#ffffff" : "#111111"
    readonly property color themeMutedTextColor: darkTheme ? "#9a9a9a" : "#57606a"
    readonly property color themeBorderColor: darkTheme ? "#505050" : "#d0d7de"
    readonly property color themeButtonColor: darkTheme ? "#393939" : "#eaeef2"
    readonly property color themeAccentColor: controller.themeAccentColor
    readonly property color themeAccentTextColor: controller.themeAccentTextColor
    readonly property color themeFocusColor: themeAccentColor
    readonly property color themeSelectionColor: themeAccentColor
    readonly property color themeSelectedTextColor: themeAccentTextColor
    readonly property color selectionDragColor: themeAccentColor
    readonly property color themeDangerColor: darkTheme ? "#ff8a80" : "#cf222e"
    readonly property color panelAccentColor: themeAccentColor
    readonly property color panelAccentTextColor: themeAccentTextColor
    readonly property int commandPaletteMaximumWidth: 620
    readonly property color markdownTextColor: controller.markdownTextColor
    readonly property string uiFontFamily: "Microsoft YaHei UI"
    readonly property int transitionDuration: controller.animationsEnabled ? 120 : 0
    readonly property int historyTriggerWidth: 12
    readonly property int historyHoverOpenDelayMs: 100
    readonly property int historyHoverCloseDelayMs: 250
    property bool historyPanelOpen: false
    property bool historyPanelOpenedByCommand: false
    property bool historyRevealHovered: false
    property bool historyPanelHovered: false
    readonly property real historyPanelWidth: Math.max(200, Math.min(360, root.width / 3))
    readonly property bool historyPanelOverlay:
        (root.width - root.marginSize * 2 - historyPanelWidth) < 320
    readonly property real editorVisibleWidth:
        root.width - root.marginSize * 2
        - (historyPanelOpen && !historyPanelOverlay ? historyPanelWidth : 0)
    readonly property bool historyPanelLoaded:
        historyPanelLoader.active && historyPanelLoader.item !== null
    readonly property int historyRevealZoneX: 0
    readonly property int historyRevealZoneWidth: marginSize + historyTriggerWidth
    readonly property bool historyPanelClipped: historyPanelLoader.clip
    readonly property bool historyRevealBlocksPointer: false
    readonly property bool historyQueryFocused:
        historyPanelLoader.item ? historyPanelLoader.item.queryFocused : false
    property string historySelectedId: ""
    property real scrollContentHeight: 0
    property bool replaceMode: false
    property string searchStatus: ""
    property bool statusTextHovered: false
    property bool statusPanelHovered: false
    property bool statusPanelOpen: false
    property bool statusCopyFeedback: false
    property bool inputScrollRestoreInProgress: false

    function scrollToBottom() {
        editorViewport.contentY = Math.max(0, editorViewport.contentHeight - editorViewport.height)
    }

    function resetScroll() {
        editorViewport.contentY = 0
    }

    function runBenchmarkAnimation() {
        animationProbe.visible = true
        animationProbe.x = marginSize
        benchmarkAnimation.restart()
    }

    function refreshScrollMetrics() {
        scrollContentHeight = editorViewport.contentHeight
    }

    function showFindPanel(withReplace) {
        commandPaletteLoader.active = false
        settingsLoader.active = false
        replaceMode = withReplace
        findPanel.visible = true
        searchStatus = ""
        Qt.callLater(function() {
            findInput.forceActiveFocus()
            findInput.selectAll()
        })
    }

    function hideFindPanel() {
        findPanel.visible = false
        editor.forceActiveFocus()
    }

    function findInDocument(backwards) {
        if (findInput.text.length === 0) {
            searchStatus = "请输入查找内容"
            return
        }
        searchStatus = controller.findNext(findInput.text, caseSensitiveToggle.enabledValue,
                                           backwards) ? "已定位" : "未找到"
    }

    function openCommandPalette() {
        findPanel.visible = false
        settingsLoader.active = false
        commandPaletteLoader.active = true
        Qt.callLater(function() {
            if (commandPaletteLoader.item) {
                commandPaletteLoader.item.activate()
            }
        })
    }

    function closeCommandPalette() {
        commandPaletteLoader.active = false
        editor.forceActiveFocus()
    }

    function openSettings() {
        findPanel.visible = false
        commandPaletteLoader.active = false
        settingsLoader.active = true
        Qt.callLater(function() {
            if (settingsLoader.item) {
                settingsLoader.item.activate()
            }
        })
    }

    function closeSettings() {
        settingsLoader.active = false
        editor.forceActiveFocus()
    }

    function openClipboardHistory(byCommand) {
        if (!controller.clipboardHistoryAvailable) {
            return
        }
        historyCloseTimer.stop()
        historyPanelOpen = true
        historyPanelOpenedByCommand = byCommand
        if (byCommand) {
            Qt.callLater(function() {
                if (historyPanelLoader.item) {
                    historyPanelLoader.item.focusQuery()
                }
            })
        }
    }

    function closeClipboardHistory() {
        historyOpenTimer.stop()
        historyCloseTimer.stop()
        historyPanelOpen = false
        historyPanelOpenedByCommand = false
        historySelectedId = ""
        controller.setClipboardHistoryFilter("")
        if (historyPanelLoader.item) {
            historyPanelLoader.item.clearQuery()
        }
    }

    function historyEdgeActivationAllowed() {
        return controller.clipboardHistoryAvailable
            && root.visible
            && !controller.historyLoadConfirmationVisible
            && !controller.historyClearConfirmationVisible
            && !settingsLoader.active
            && !commandPaletteLoader.active
    }

    function beginClipboardHistoryEdgeHover() {
        historyCloseTimer.stop()
        if (historyEdgeActivationAllowed() && !historyPanelOpen) {
            historyOpenTimer.restart()
        }
    }

    function endClipboardHistoryEdgeHover() {
        historyOpenTimer.stop()
        scheduleClipboardHistoryClose()
    }

    function scheduleClipboardHistoryClose() {
        if (historyPanelOpen && !historyPanelOpenedByCommand
            && !historyRevealHovered && !historyPanelHovered) {
            historyCloseTimer.restart()
        }
    }

    function handleClipboardHistoryLeftEdgeExit() {
        historyOpenTimer.stop()
        historyCloseTimer.stop()
        if (historyEdgeActivationAllowed() && !historyPanelOpen) {
            openClipboardHistory(false)
        }
    }

    function handleEscapeAction() {
        if (controller.historyLoadConfirmationVisible) {
            controller.cancelLoadClipboardHistory()
        } else if (controller.historyClearConfirmationVisible) {
            controller.cancelClearClipboardHistory()
        } else if (root.historyPanelOpen) {
            root.closeClipboardHistory()
            editor.forceActiveFocus()
        } else {
            controller.hideEditor()
        }
    }

    function dispatchHistoryTestAction(action, value) {
        if (!historyPanelLoader.item) {
            return false
        }
        if (action === "historyHoverTriggerEnter") {
            historyRevealHovered = true
            beginClipboardHistoryEdgeHover()
        } else if (action === "historyHoverTriggerLeave") {
            historyRevealHovered = false
            endClipboardHistoryEdgeHover()
        } else if (action === "historyPanelEnter") {
            historyPanelHovered = true
            historyCloseTimer.stop()
        } else if (action === "historyPanelLeave") {
            historyPanelHovered = false
            scheduleClipboardHistoryClose()
        } else if (action === "historyOpen") {
            openClipboardHistory(true)
        } else if (action === "historyClose") {
            closeClipboardHistory()
        } else if (action === "historySetQuery") {
            historyPanelLoader.item.setQuery(value)
        } else if (action === "historySelect") {
            historyPanelLoader.item.selectId(value)
        } else if (action === "historyActivateSelected") {
            historyPanelLoader.item.activateSelected()
        } else if (action === "historyDoubleClick") {
            historyPanelLoader.item.selectId(value)
            historyPanelLoader.item.activateSelected()
        } else if (action === "historyConfirmLoad") {
            controller.confirmLoadClipboardHistory()
        } else if (action === "historyCancelLoad") {
            controller.cancelLoadClipboardHistory()
        } else if (action === "historyDeleteSelected") {
            controller.deleteClipboardHistoryItem(historySelectedId)
        } else if (action === "historyRequestClear") {
            controller.requestClearClipboardHistory()
        } else if (action === "historyConfirmClear") {
            controller.confirmClearClipboardHistory()
        } else if (action === "historyCancelClear") {
            controller.cancelClearClipboardHistory()
        } else if (action === "historyEscape") {
            handleEscapeAction()
        } else {
            return false
        }
        return true
    }

    Component.onCompleted: {
        controller.registerWindow(root)
        controller.registerEditor(editor)
        refreshScrollMetrics()
    }

    Timer {
        id: scrollMetricsTimer
        interval: 60
        repeat: false
        onTriggered: root.refreshScrollMetrics()
    }

    Timer {
        id: historyOpenTimer
        interval: root.historyHoverOpenDelayMs
        repeat: false
        onTriggered: {
            if (root.historyEdgeActivationAllowed()) {
                root.openClipboardHistory(false)
            }
        }
    }

    Timer {
        id: historyCloseTimer
        interval: root.historyHoverCloseDelayMs
        repeat: false
        onTriggered: {
            if (!root.historyPanelOpenedByCommand
                && !root.historyRevealHovered && !root.historyPanelHovered) {
                root.closeClipboardHistory()
            }
        }
    }

    Connections {
        target: editorViewport

        function onContentHeightChanged() {
            scrollMetricsTimer.restart()
        }

        function onHeightChanged() {
            scrollMetricsTimer.restart()
        }
    }

    Timer {
        id: statusPanelShowTimer
        interval: controller.statusPanelShowDelayMs
        repeat: false
        onTriggered: {
            if (root.statusTextHovered) {
                root.statusPanelOpen = true
            }
        }
    }

    Timer {
        id: statusPanelHideTimer
        interval: controller.statusPanelHideDelayMs
        repeat: false
        onTriggered: {
            if (!root.statusTextHovered && !root.statusPanelHovered) {
                root.statusPanelOpen = false
                root.statusCopyFeedback = false
            }
        }
    }

    onClosing: function(close) {
        close.accepted = false
        controller.hideEditor()
    }

    onVisibleChanged: {
        if (!visible && historyPanelOpen) {
            closeClipboardHistory()
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible && !findPanel.visible && !commandPaletteLoader.active
                 && !settingsLoader.active
        onActivated: root.handleEscapeAction()
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.WindowShortcut
        enabled: root.visible && !findPanel.visible && !commandPaletteLoader.active
                 && !settingsLoader.active
        onActivated: controller.deliverAndHide()
    }

    Shortcut {
        sequence: "Ctrl+W"
        context: Qt.WindowShortcut
        enabled: root.visible && !findPanel.visible && !commandPaletteLoader.active
                 && !settingsLoader.active
        onActivated: controller.discardAndHide()
    }

    Shortcut {
        sequence: "F3"
        context: Qt.WindowShortcut
        enabled: root.visible && findPanel.visible
        onActivated: root.findInDocument(false)
    }

    Shortcut {
        sequence: "Shift+F3"
        context: Qt.WindowShortcut
        enabled: root.visible && findPanel.visible
        onActivated: root.findInDocument(true)
    }

    Instantiator {
        model: controller.commands

        delegate: Shortcut {
            required property var modelData
            sequence: modelData.shortcut
            context: Qt.WindowShortcut
            enabled: root.visible && sequence.length > 0
            onActivated: controller.executeCommand(modelData.id)
        }
    }

    Connections {
        target: controller

        function onUiCommandRequested(commandId) {
            if (commandId === "find") {
                root.showFindPanel(false)
            } else if (commandId === "replace") {
                root.showFindPanel(true)
            } else if (commandId === "commandPalette") {
                root.openCommandPalette()
            } else if (commandId === "settings") {
                root.openSettings()
            } else if (commandId === "clipboardHistory") {
                if (root.historyPanelOpen) {
                    if (controller.historyLoadConfirmationVisible) {
                        controller.cancelLoadClipboardHistory()
                    } else if (controller.historyClearConfirmationVisible) {
                        controller.cancelClearClipboardHistory()
                    }
                    root.closeClipboardHistory()
                    editor.forceActiveFocus()
                } else {
                    root.openClipboardHistory(true)
                }
            }
        }

        function onClipboardHistoryLoaded() {
            root.closeClipboardHistory()
            editor.forceActiveFocus()
        }

        function onClipboardHistoryLeftEdgeExited() {
            root.handleClipboardHistoryLeftEdgeExit()
        }

        function onStatusMessageChanged() {
            root.statusCopyFeedback = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.themeBackgroundColor

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }
    }

    Rectangle {
        id: header
        z: 10
        x: root.resizeMargin
        y: root.resizeMargin
        width: root.width - root.resizeMargin * 2
        height: root.dragZoneHeight - root.resizeMargin
        color: root.themeHeaderColor

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: root.marginSize - root.resizeMargin
            anchors.verticalCenter: parent.verticalCenter
            text: controller.externalFileMode
                  ? (controller.externalCliType.length > 0
                     ? "外部提示词编辑器 · " + controller.externalCliType
                     : "外部提示词编辑器")
                  : "临时编辑器"
            color: root.themeStrongTextColor
            font.family: root.uiFontFamily
            font.pointSize: 11
            font.weight: Font.DemiBold
        }

        Text {
            id: statusText
            anchors.right: parent.right
            anchors.rightMargin: root.marginSize - root.resizeMargin
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(360, parent.width * 0.55)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: controller.statusHealthy ? controller.statusPanelSummary : controller.statusMessage
            color: controller.statusHealthy ? root.themeMutedTextColor : root.themeDangerColor
            font.family: root.uiFontFamily
            font.pointSize: 9

            HoverHandler {
                onHoveredChanged: {
                    root.statusTextHovered = hovered
                    if (hovered) {
                        statusPanelHideTimer.stop()
                        statusPanelShowTimer.restart()
                    } else {
                        statusPanelShowTimer.stop()
                        statusPanelHideTimer.restart()
                    }
                }
            }
        }

        Rectangle {
            id: statusPanel
            z: 40
            anchors.top: statusText.bottom
            anchors.topMargin: 6
            anchors.right: statusText.right
            width: Math.min(controller.statusPanelMaxWidth,
                            header.width - statusText.x - 6)
            height: Math.min(contentHeight, root.height - y - 12)
            radius: 5
            color: root.themePanelColor
            border.color: root.themeBorderColor
            border.width: 1
            visible: root.statusPanelOpen
            opacity: root.statusPanelOpen ? 1 : 0
            clip: true

            property real contentHeight: (controller.statusHealthy
                                          ? normalColumn.implicitHeight
                                          : errorText.implicitHeight) + 20

            Behavior on opacity {
                NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
            }

            Behavior on color {
                ColorAnimation { duration: root.transitionDuration }
            }

            HoverHandler {
                onHoveredChanged: {
                    root.statusPanelHovered = hovered
                    if (hovered) {
                        statusPanelHideTimer.stop()
                    } else {
                        statusPanelHideTimer.restart()
                    }
                }
            }

            Column {
                id: normalColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5
                visible: controller.statusHealthy

                Repeater {
                    model: controller.statusPanelHints

                    delegate: Text {
                        required property string modelData
                        width: normalColumn.width
                        text: modelData
                        color: root.themeTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: controller.statusPanelFontSize
                        wrapMode: Text.Wrap
                    }
                }

                Text {
                    width: normalColumn.width
                    text: controller.statusPanelSummary
                    color: root.themeMutedTextColor
                    font.family: root.uiFontFamily
                    font.pointSize: controller.statusPanelFontSize
                    wrapMode: Text.Wrap
                }
            }

            Item {
                id: errorColumn
                anchors.fill: parent
                visible: !controller.statusHealthy

                Text {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.statusCopyFeedback ? "已复制" : controller.statusMessage
                    color: root.statusCopyFeedback ? root.themeAccentColor : root.themeDangerColor
                    font.family: root.uiFontFamily
                    font.pointSize: controller.statusPanelFontSize
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (controller.copyToClipboard(controller.statusMessage)) {
                            root.statusCopyFeedback = true
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: root.startSystemMove()
        }
    }

    Rectangle {
        id: findPanel
        z: 60
        visible: false
        x: Math.round((root.width - width) / 2)
        y: root.dragZoneHeight + 8
        width: Math.min(760, root.width - 48)
        height: root.replaceMode ? 104 : 66
        radius: 4
        color: root.themePanelColor
        border.color: root.themeBorderColor
        border.width: 1
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
        }

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }

        Rectangle {
            id: findFieldFrame
            x: 12
            y: 10
            width: findPanel.width - 328
            height: 32
            radius: 3
            color: root.themeFieldColor
            border.color: findInput.activeFocus ? root.themeFocusColor : root.themeBorderColor

            TextInput {
                id: findInput
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                verticalAlignment: TextInput.AlignVCenter
                color: root.themeTextColor
                selectionColor: root.themeSelectionColor
                selectedTextColor: root.themeSelectedTextColor
                font.family: root.uiFontFamily
                font.pointSize: 10
                selectByMouse: true
                clip: true

                Keys.onReturnPressed: root.findInDocument(false)
                Keys.onEnterPressed: root.findInDocument(false)
                Keys.onEscapePressed: root.hideFindPanel()
            }
        }

        Rectangle {
            id: caseSensitiveToggle
            property bool enabledValue: false
            x: findFieldFrame.x + findFieldFrame.width + 8
            y: 10
            width: 42
            height: 32
            radius: 3
            color: enabledValue ? root.themeAccentColor : root.themeButtonColor

            Text {
                anchors.centerIn: parent
                text: "Aa"
                color: root.themeStrongTextColor
                font.family: "Cascadia Mono"
                font.pointSize: 9
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: caseSensitiveToggle.enabledValue = !caseSensitiveToggle.enabledValue
            }
        }

        Rectangle {
            x: caseSensitiveToggle.x + caseSensitiveToggle.width + 8
            y: 10
            width: 54
            height: 32
            radius: 3
            color: root.themeButtonColor
            Text { anchors.centerIn: parent; text: "上一个"; color: root.themeTextColor; font.pointSize: 9 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.findInDocument(true)
            }
        }

        Rectangle {
            x: caseSensitiveToggle.x + caseSensitiveToggle.width + 70
            y: 10
            width: 54
            height: 32
            radius: 3
            color: root.themeAccentColor
            Text { anchors.centerIn: parent; text: "下一个"; color: "#ffffff"; font.pointSize: 9 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.findInDocument(false)
            }
        }

        Rectangle {
            x: findPanel.width - 42
            y: 10
            width: 30
            height: 32
            radius: 3
            color: root.themeButtonColor
            Text { anchors.centerIn: parent; text: "×"; color: root.themeTextColor; font.pointSize: 13 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.hideFindPanel()
            }
        }

        Rectangle {
            id: replaceFieldFrame
            visible: root.replaceMode
            x: 12
            y: 52
            width: findFieldFrame.width
            height: 32
            radius: 3
            color: root.themeFieldColor
            border.color: replaceInput.activeFocus ? root.themeFocusColor : root.themeBorderColor

            TextInput {
                id: replaceInput
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                verticalAlignment: TextInput.AlignVCenter
                color: root.themeTextColor
                selectionColor: root.themeSelectionColor
                selectedTextColor: root.themeSelectedTextColor
                font.family: root.uiFontFamily
                font.pointSize: 10
                selectByMouse: true
                clip: true
                Keys.onEscapePressed: root.hideFindPanel()
            }
        }

        Rectangle {
            visible: root.replaceMode
            x: replaceFieldFrame.x + replaceFieldFrame.width + 8
            y: 52
            width: 70
            height: 32
            radius: 3
            color: root.themeButtonColor
            Text { anchors.centerIn: parent; text: "替换当前"; color: root.themeTextColor; font.pointSize: 9 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.searchStatus = controller.replaceCurrent(
                        findInput.text, replaceInput.text, caseSensitiveToggle.enabledValue
                    ) ? "已处理当前匹配" : "未找到"
                }
            }
        }

        Rectangle {
            visible: root.replaceMode
            x: replaceFieldFrame.x + replaceFieldFrame.width + 86
            y: 52
            width: 70
            height: 32
            radius: 3
            color: root.themeAccentColor
            Text { anchors.centerIn: parent; text: "全部替换"; color: "#ffffff"; font.pointSize: 9 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    const count = controller.replaceAll(
                        findInput.text, replaceInput.text, caseSensitiveToggle.enabledValue
                    )
                    root.searchStatus = "已替换 " + count + " 处"
                }
            }
        }

        Text {
            x: findPanel.width - 154
            y: root.replaceMode ? 86 : 47
            width: 142
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: root.searchStatus
            color: root.themeMutedTextColor
            font.family: root.uiFontFamily
            font.pointSize: 8
        }
    }

    Rectangle {
        id: editorSurface
        x: root.marginSize
           + (root.historyPanelOpen && !root.historyPanelOverlay
              ? root.historyPanelWidth : 0)
        y: root.dragZoneHeight
        width: root.editorVisibleWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        color: root.themeEditorSurfaceColor

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }
        Behavior on x { NumberAnimation { duration: root.transitionDuration } }
        Behavior on width { NumberAnimation { duration: root.transitionDuration } }
    }

    Flickable {
        id: editorViewport
        x: root.marginSize
           + (root.historyPanelOpen && !root.historyPanelOverlay
              ? root.historyPanelWidth : 0)
        y: root.dragZoneHeight
        width: root.editorVisibleWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        // 正文下方保留 2/3 页可滚动的留白区，配合 PageUp/PageDown 纯滚动浏览；
        // 触底自动滚动后光标停在视口上 1/3（段尾触底即滚到此处的最大位置）。
        contentHeight: Math.max(height, editor.contentHeight + height * 2 / 3)
        pixelAligned: true

        Behavior on x { NumberAnimation { duration: root.transitionDuration } }
        Behavior on width { NumberAnimation { duration: root.transitionDuration } }

        TextEdit {
            id: editor
            objectName: "scratchText"
            property int selectionDragPosition: -1
            readonly property rect selectionDragRectangle:
                selectionDragPosition >= 0
                    ? positionToRectangle(selectionDragPosition)
                    : Qt.rect(0, 0, 0, 0)
            x: 12
            y: 8
            width: editorViewport.width - 24
            height: Math.max(editorViewport.height - 16, contentHeight)
            color: root.markdownTextColor
            selectionColor: root.themeSelectionColor
            selectedTextColor: root.themeSelectedTextColor
            font.family: controller.editorFontFamily
            font.pointSize: controller.editorFontPointSize
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: true
            inputMethodHints: Qt.ImhMultiLine

            onCursorRectangleChanged: {
                // 撤销回滚滚动位置期间，光标跟随可能在下一帧延迟触发并覆盖
                // 恢复结果，这里先跳过；由 C++ 在回滚窗口结束后恢复。
                if (root.inputScrollRestoreInProgress) {
                    return
                }
                // 文档变更期间 QML 会短暂给出过期/无效的光标矩形（如落在
                // 文档开头附近），若直接跟随会把视图拉回顶部。用 positionAt
                // 反查该矩形对应的位置并与当前光标位置比对，不一致则跳过。
                const rectPosition = positionAt(cursorRectangle.x, cursorRectangle.y)
                if (Math.abs(rectPosition - cursorPosition) > 4) {
                    return
                }
                const top = cursorRectangle.y
                const bottom = cursorRectangle.y + cursorRectangle.height
                if (top < editorViewport.contentY) {
                    editorViewport.contentY = Math.max(0, top)
                } else if (bottom > editorViewport.contentY + editorViewport.height) {
                    editorViewport.contentY = Math.min(
                        Math.max(0, editorViewport.contentHeight - editorViewport.height),
                        bottom - editorViewport.height
                    )
                }
            }

            Rectangle {
                z: 2
                x: Math.round(editor.selectionDragRectangle.x)
                y: Math.round(editor.selectionDragRectangle.y)
                width: 2
                height: Math.max(1, editor.selectionDragRectangle.height)
                visible: editor.selectionDragPosition >= 0
                color: root.selectionDragColor
            }
        }
    }

    Loader {
        id: historyPanelLoader
        z: 35
        x: root.marginSize
        y: root.dragZoneHeight
        width: root.historyPanelWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        clip: true
        active: controller.clipboardHistoryAvailable

        sourceComponent: Item {
            id: historyRoot
            readonly property bool queryFocused: historyQuery.activeFocus

            function focusQuery() {
                historyQuery.forceActiveFocus()
                historyQuery.selectAll()
            }
            function clearQuery() { historyQuery.text = "" }
            function setQuery(value) {
                historyQuery.text = value === undefined ? "" : value
                controller.setClipboardHistoryFilter(historyQuery.text)
            }
            function selectId(value) {
                root.historySelectedId = value
                controller.selectClipboardHistoryItem(value)
                for (let i = 0; i < historyList.count; ++i) {
                    const row = historyList.itemAtIndex(i)
                    if (row && row.itemId === value) {
                        historyList.currentIndex = i
                        return
                    }
                }
            }
            function activateSelected() {
                if (root.historySelectedId.length > 0) {
                    controller.requestLoadClipboardHistory(root.historySelectedId)
                }
            }
            function moveSelection(delta) {
                if (historyList.count === 0) return
                historyList.currentIndex = Math.max(
                    0, Math.min(historyList.count - 1, historyList.currentIndex + delta))
                const row = historyList.itemAtIndex(historyList.currentIndex)
                if (row) selectId(row.itemId)
            }

            Rectangle {
                id: historyPanel
                x: root.historyPanelOpen ? 0 : -root.historyPanelWidth
                width: root.historyPanelWidth
                height: parent.height
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: 1

                Behavior on x {
                    NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
                }

                HoverHandler {
                    onHoveredChanged: {
                        root.historyPanelHovered = hovered
                        if (hovered) historyCloseTimer.stop()
                        else root.scheduleClipboardHistoryClose()
                    }
                }

                Text {
                    id: historyTitle
                    x: 14
                    y: 12
                    text: "剪贴板历史"
                    color: root.themeStrongTextColor
                    font.family: root.uiFontFamily
                    font.pointSize: 11
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    id: historySearchFrame
                    x: 12
                    y: 40
                    width: parent.width - 24
                    height: 34
                    radius: 4
                    color: root.themeFieldColor
                    border.color: historyQuery.activeFocus
                                  ? root.themeFocusColor : root.themeBorderColor

                    TextInput {
                        id: historyQuery
                        anchors.fill: parent
                        anchors.margins: 8
                        color: root.themeTextColor
                        selectionColor: root.themeSelectionColor
                        selectedTextColor: root.themeSelectedTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: 9
                        clip: true
                        onTextChanged: controller.setClipboardHistoryFilter(text)
                        Keys.onDownPressed: function(event) {
                            historyRoot.moveSelection(1); event.accepted = true
                        }
                        Keys.onUpPressed: function(event) {
                            historyRoot.moveSelection(-1); event.accepted = true
                        }
                        Keys.onReturnPressed: function(event) {
                            historyRoot.activateSelected(); event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            historyRoot.activateSelected(); event.accepted = true
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: historyQuery.text.length === 0
                        text: "搜索完整文本"
                        color: root.themeMutedTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: 9
                    }
                }

                ListView {
                    id: historyList
                    x: 8
                    y: 82
                    width: parent.width - 16
                    height: parent.height - 132
                    clip: true
                    spacing: 4
                    model: controller.clipboardHistoryModel
                    currentIndex: count > 0 ? Math.max(0, currentIndex) : -1
                    onCountChanged: {
                        if (count > 0 && root.historySelectedId.length === 0) {
                            currentIndex = 0
                            const row = itemAtIndex(0)
                            if (row) historyRoot.selectId(row.itemId)
                        }
                    }

                    delegate: Rectangle {
                        id: historyRow
                        required property string historyId
                        required property string previewText
                        required property double capturedAtMs
                        required property int characterCount
                        property string itemId: historyId
                        width: historyList.width
                        height: controller.historyCardHeight
                        radius: 4
                        color: root.historySelectedId === historyId
                               ? root.themeButtonColor : "transparent"
                        border.color: root.historySelectedId === historyId
                                      ? root.themeAccentColor : "transparent"

                        Text {
                            x: 8
                            y: 4
                            width: parent.width - 16
                            height: parent.height - 22
                            text: historyRow.previewText
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 9
                            wrapMode: Text.Wrap
                            maximumLineCount: Math.max(1, Math.min(
                                3, Math.floor((parent.height - 22) / 18)))
                            elide: Text.ElideRight
                        }
                        Text {
                            x: 8
                            y: parent.height - 16
                            width: parent.width - 16
                            text: new Date(historyRow.capturedAtMs).toLocaleString(
                                      Qt.locale(), Locale.ShortFormat)
                                  + " · " + historyRow.characterCount + " 字"
                            color: root.themeMutedTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 8
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onClicked: historyRoot.selectId(historyRow.historyId)
                            onDoubleClicked: {
                                historyRoot.selectId(historyRow.historyId)
                                historyRoot.activateSelected()
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: historyList
                    visible: historyList.count === 0
                    text: controller.clipboardHistoryError.length > 0
                          ? controller.clipboardHistoryError : "暂无剪贴板历史"
                    color: controller.clipboardHistoryError.length > 0
                           ? root.themeDangerColor : root.themeMutedTextColor
                    width: historyList.width - 24
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.uiFontFamily
                    font.pointSize: 9
                }

                Rectangle {
                    x: 12
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    width: 72
                    height: 30
                    radius: 4
                    color: root.themeButtonColor
                    Text { anchors.centerIn: parent; text: "删除"; color: root.themeTextColor; font.pointSize: 9 }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.historySelectedId.length > 0
                        onClicked: controller.deleteClipboardHistoryItem(root.historySelectedId)
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    width: 72
                    height: 30
                    radius: 4
                    color: root.themeButtonColor
                    Text { anchors.centerIn: parent; text: "清空"; color: root.themeDangerColor; font.pointSize: 9 }
                    MouseArea { anchors.fill: parent; onClicked: controller.requestClearClipboardHistory() }
                }
            }

        }
    }

    // Hover-only edge proximity area. It deliberately spans the resize/drag
    // frame but never accepts button events, so the existing native window
    // interactions keep their normal precedence.
    Item {
        id: historyRevealZone
        z: 36
        x: root.historyRevealZoneX
        y: root.dragZoneHeight
        width: root.historyRevealZoneWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        visible: controller.clipboardHistoryAvailable

        HoverHandler {
            target: null
            blocking: root.historyRevealBlocksPointer
            onHoveredChanged: {
                root.historyRevealHovered = hovered
                if (hovered) {
                    if (point.pressedButtons === Qt.NoButton) {
                        root.beginClipboardHistoryEdgeHover()
                    }
                } else {
                    root.endClipboardHistoryEdgeHover()
                }
            }
        }
    }

    Rectangle {
        z: 95
        anchors.fill: parent
        visible: controller.historyLoadConfirmationVisible
        color: "#88000000"
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 48)
            height: 140
            radius: 6
            color: root.themePanelColor
            border.color: root.themeBorderColor
            Text {
                anchors.top: parent.top; anchors.topMargin: 24
                anchors.horizontalCenter: parent.horizontalCenter
                text: "放弃当前修改并载入历史？"
                color: root.themeTextColor; font.family: root.uiFontFamily
            }
            Rectangle {
                x: 54; y: 82; width: 120; height: 34; radius: 4
                color: root.themeButtonColor
                Text { anchors.centerIn: parent; text: "取消"; color: root.themeTextColor }
                MouseArea { anchors.fill: parent; onClicked: controller.cancelLoadClipboardHistory() }
            }
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 54
                y: 82; width: 120; height: 34; radius: 4
                color: root.themeAccentColor
                Text { anchors.centerIn: parent; text: "载入"; color: root.themeAccentTextColor }
                MouseArea { anchors.fill: parent; onClicked: controller.confirmLoadClipboardHistory() }
            }
        }
    }

    Rectangle {
        z: 95
        anchors.fill: parent
        visible: controller.historyClearConfirmationVisible
        color: "#88000000"
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 48)
            height: 140
            radius: 6
            color: root.themePanelColor
            border.color: root.themeBorderColor
            Text {
                anchors.top: parent.top; anchors.topMargin: 24
                anchors.horizontalCenter: parent.horizontalCenter
                text: "确认清空全部剪贴板历史？"
                color: root.themeTextColor; font.family: root.uiFontFamily
            }
            Rectangle {
                x: 54; y: 82; width: 120; height: 34; radius: 4
                color: root.themeButtonColor
                Text { anchors.centerIn: parent; text: "取消"; color: root.themeTextColor }
                MouseArea { anchors.fill: parent; onClicked: controller.cancelClearClipboardHistory() }
            }
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 54
                y: 82; width: 120; height: 34; radius: 4
                color: root.themeDangerColor
                Text { anchors.centerIn: parent; text: "清空"; color: "white" }
                MouseArea { anchors.fill: parent; onClicked: controller.confirmClearClipboardHistory() }
            }
        }
    }

    // The outermost strip keeps normal window resizing.  The narrow frame just
    // inside it is intentionally draggable, so the window can be moved from
    // any side without stealing pointer events from the editor itself.
    MouseArea {
        z: 19
        x: root.resizeMargin
        y: root.dragZoneHeight
        width: root.edgeDragWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemMove()
    }

    MouseArea {
        z: 19
        x: root.width - root.marginSize
        y: root.dragZoneHeight
        width: root.edgeDragWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemMove()
    }

    MouseArea {
        z: 19
        x: root.resizeMargin
        y: root.height - root.marginSize
        width: root.width - root.resizeMargin * 2
        height: root.edgeDragWidth
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemMove()
    }

    Rectangle {
        id: scrollThumb
        z: 20
        // Keep the opaque thumb in the reserved margin so it does not force a
        // blended render pass over the large text scene.
        x: root.width - root.marginSize + 2
        y: editorViewport.y
            + (editorViewport.contentY
               / Math.max(1, editorViewport.contentHeight - editorViewport.height))
              * Math.max(0, editorViewport.height - height)
        width: 5
        height: Math.max(
            28,
            editorViewport.height * editorViewport.height
                / Math.max(editorViewport.height, root.scrollContentHeight)
        )
        radius: 0
        visible: root.scrollContentHeight > editorViewport.height + 0.5
        color: scrollHover.hovered || editorViewport.movingVertically
               ? (root.darkTheme ? "#8b8b8b" : "#6e7781")
               : (root.darkTheme ? "#555555" : "#afb8c1")

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }

        HoverHandler {
            id: scrollHover
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            property real grabOffset: 0

            onPressed: function(mouse) {
                grabOffset = mouse.y
            }
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }
                const trackRange = Math.max(1, editorViewport.height - scrollThumb.height)
                const requestedTop = Math.max(
                    editorViewport.y,
                    Math.min(
                        editorViewport.y + trackRange,
                        scrollThumb.y + mouse.y - grabOffset
                    )
                )
                editorViewport.contentY =
                    (requestedTop - editorViewport.y) / trackRange
                    * Math.max(0, editorViewport.contentHeight - editorViewport.height)
            }
        }
    }

    Rectangle {
        id: animationProbe
        visible: false
        z: 10
        x: root.marginSize
        y: root.dragZoneHeight - 4
        width: 40
        height: 2
        radius: 1
        color: root.themeAccentColor
    }

    NumberAnimation {
        id: benchmarkAnimation
        target: animationProbe
        from: root.marginSize
        to: Math.max(root.marginSize, root.width - root.marginSize - animationProbe.width)
        duration: 1000
        easing.type: Easing.Linear
        onFinished: {
            animationProbe.visible = false
            controller.animationBenchmarkFinished()
        }
    }

    Loader {
        id: settingsLoader
        z: 90
        anchors.fill: parent
        active: false
        sourceComponent: settingsComponent
    }

    Component {
        id: settingsComponent

        Item {
            id: settingsRoot
            focus: true
            opacity: 0
            property string draftTheme: "dark"
            property string draftFontFamily: ""
            property int draftFontPointSize: 13
            property bool draftAnimationsEnabled: true
            property int draftStatusPanelFontSize: 10
            property int draftStatusPanelShowDelayMs: 300
            property int draftStatusPanelHideDelayMs: 250
            property int draftStatusPanelMaxWidth: 360
            property string saveStatus: ""

            Behavior on opacity {
                NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
            }

            function activate() {
                draftTheme = controller.theme
                draftFontFamily = controller.editorFontFamily
                draftFontPointSize = controller.editorFontPointSize
                draftAnimationsEnabled = controller.animationsEnabled
                draftStatusPanelFontSize = controller.statusPanelFontSize
                draftStatusPanelShowDelayMs = controller.statusPanelShowDelayMs
                draftStatusPanelHideDelayMs = controller.statusPanelHideDelayMs
                draftStatusPanelMaxWidth = controller.statusPanelMaxWidth
                fontFamilyInput.text = draftFontFamily
                fontSizeInput.text = draftFontPointSize.toString()
                statusPanelFontSizeInput.text = draftStatusPanelFontSize.toString()
                statusPanelShowDelayInput.text = draftStatusPanelShowDelayMs.toString()
                statusPanelHideDelayInput.text = draftStatusPanelHideDelayMs.toString()
                statusPanelMaxWidthInput.text = draftStatusPanelMaxWidth.toString()
                saveStatus = ""
                opacity = 1
                forceActiveFocus()
            }

            function save() {
                const requestedSize = Number(fontSizeInput.text)
                const panelFontSize = Number(statusPanelFontSizeInput.text)
                const panelShowDelayMs = Number(statusPanelShowDelayInput.text)
                const panelHideDelayMs = Number(statusPanelHideDelayInput.text)
                const panelMaxWidth = Number(statusPanelMaxWidthInput.text)
                if (controller.applyAppearance(draftTheme, fontFamilyInput.text,
                                               requestedSize, draftAnimationsEnabled)
                    && controller.applyStatusPanelSettings(panelFontSize, panelShowDelayMs,
                                                           panelHideDelayMs, panelMaxWidth)) {
                    saveStatus = "设置已保存"
                    draftFontFamily = controller.editorFontFamily
                    draftFontPointSize = controller.editorFontPointSize
                    draftStatusPanelFontSize = controller.statusPanelFontSize
                    draftStatusPanelShowDelayMs = controller.statusPanelShowDelayMs
                    draftStatusPanelHideDelayMs = controller.statusPanelHideDelayMs
                    draftStatusPanelMaxWidth = controller.statusPanelMaxWidth
                } else {
                    saveStatus = controller.settingsError
                }
            }

            Keys.onEscapePressed: root.closeSettings()

            Rectangle {
                anchors.fill: parent
                color: "#88000000"
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.closeSettings()
                }
            }

            Rectangle {
                id: settingsPanel
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: Math.min(640, parent.width - 40)
                height: Math.min(430, parent.height - 32)
                radius: 7
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: 1

                Behavior on color {
                    ColorAnimation { duration: root.transitionDuration }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) { mouse.accepted = true }
                }

                Text {
                    x: 20
                    y: 16
                    text: "设置"
                    color: root.themeStrongTextColor
                    font.family: root.uiFontFamily
                    font.pointSize: 13
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    y: 18
                    text: "×"
                    color: root.themeMutedTextColor
                    font.pointSize: 13
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeSettings()
                    }
                }

                Flickable {
                    id: settingsContent
                    x: 20
                    y: 52
                    width: parent.width - 40
                    height: parent.height - 112
                    contentWidth: width
                    contentHeight: 425
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Item {
                        width: settingsContent.width
                        height: settingsContent.contentHeight

                        Text {
                            x: 0
                            y: 8
                            width: 118
                            text: "主题"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 0
                            width: 86
                            height: 34
                            radius: 4
                            color: settingsRoot.draftTheme === "dark"
                                   ? root.panelAccentColor : root.themeButtonColor
                            border.color: root.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: "深色"
                                color: settingsRoot.draftTheme === "dark"
                                       ? root.panelAccentTextColor : root.themeTextColor
                                font.family: root.uiFontFamily
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: settingsRoot.draftTheme = "dark"
                            }
                        }

                        Rectangle {
                            x: 220
                            y: 0
                            width: 86
                            height: 34
                            radius: 4
                            color: settingsRoot.draftTheme === "light"
                                   ? root.panelAccentColor : root.themeButtonColor
                            border.color: root.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: "浅色"
                                color: settingsRoot.draftTheme === "light"
                                       ? root.panelAccentTextColor : root.themeTextColor
                                font.family: root.uiFontFamily
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: settingsRoot.draftTheme = "light"
                            }
                        }

                        Text {
                            x: 0
                            y: 57
                            width: 118
                            text: "编辑字体"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 48
                            width: parent.width - 126
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: fontFamilyInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: fontFamilyInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                selectByMouse: true
                                clip: true
                            }
                        }

                        Text {
                            x: 0
                            y: 105
                            width: 118
                            text: "字号（9–24）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 96
                            width: 86
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: fontSizeInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: fontSizeInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                validator: IntValidator { bottom: 9; top: 24 }
                            }
                        }

                        Text {
                            x: 0
                            y: 153
                            width: 118
                            text: "轻量动画"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 144
                            width: 86
                            height: 34
                            radius: 17
                            color: settingsRoot.draftAnimationsEnabled
                                   ? root.panelAccentColor : root.themeButtonColor
                            border.color: root.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: settingsRoot.draftAnimationsEnabled ? "开启" : "关闭"
                                color: settingsRoot.draftAnimationsEnabled
                                       ? root.panelAccentTextColor : root.themeTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: settingsRoot.draftAnimationsEnabled =
                                           !settingsRoot.draftAnimationsEnabled
                            }
                        }

                        Text {
                            x: 0
                            y: 195
                            width: 118
                            text: "面板字号（9–24）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 186
                            width: 110
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: statusPanelFontSizeInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: statusPanelFontSizeInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                validator: IntValidator { bottom: 9; top: 24 }
                            }
                        }

                        Text {
                            x: 0
                            y: 243
                            width: 118
                            text: "显示延迟（毫秒）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 234
                            width: 110
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: statusPanelShowDelayInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: statusPanelShowDelayInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                validator: IntValidator { bottom: 0; top: 2000 }
                            }
                        }

                        Text {
                            x: 0
                            y: 291
                            width: 118
                            text: "收起延迟（毫秒）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 282
                            width: 110
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: statusPanelHideDelayInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: statusPanelHideDelayInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                validator: IntValidator { bottom: 0; top: 3000 }
                            }
                        }

                        Text {
                            x: 0
                            y: 339
                            width: 118
                            text: "最大宽度（像素）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Rectangle {
                            x: 126
                            y: 330
                            width: 110
                            height: 34
                            radius: 4
                            color: root.themeFieldColor
                            border.color: statusPanelMaxWidthInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: statusPanelMaxWidthInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: 10
                                validator: IntValidator { bottom: 200; top: 800 }
                            }
                        }

                        Text {
                            x: 0
                            y: 384
                            text: "集中配置文件"
                            color: root.themeMutedTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 8
                        }

                        Text {
                            x: 0
                            y: 405
                            width: parent.width
                            text: controller.settingsFile
                            color: root.themeMutedTextColor
                            font.family: "Cascadia Mono"
                            font.pointSize: 8
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Text {
                    x: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 19
                    width: parent.width - 300
                    text: settingsRoot.saveStatus
                    color: settingsRoot.saveStatus === "设置已保存"
                           ? root.themeMutedTextColor : root.themeDangerColor
                    font.family: root.uiFontFamily
                    font.pointSize: 9
                    elide: Text.ElideRight
                }

                Rectangle {
                    anchors.right: applySettingsButton.left
                    anchors.rightMargin: 10
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    width: 92
                    height: 36
                    radius: 4
                    color: root.themeButtonColor
                    border.color: root.themeBorderColor
                    Text {
                        anchors.centerIn: parent
                        text: "恢复默认"
                        color: root.themeTextColor
                        font.family: root.uiFontFamily
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            controller.resetAppearance()
                            controller.resetStatusPanelSettings()
                            settingsRoot.activate()
                            settingsRoot.saveStatus = "已恢复默认设置"
                        }
                    }
                }

                Rectangle {
                    id: applySettingsButton
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    width: 92
                    height: 36
                    radius: 4
                    color: root.panelAccentColor
                    Text {
                        anchors.centerIn: parent
                        text: "应用"
                        color: root.panelAccentTextColor
                        font.family: root.uiFontFamily
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsRoot.save()
                    }
                }
            }
        }
    }

    Loader {
        id: commandPaletteLoader
        z: 80
        anchors.fill: parent
        active: false
        sourceComponent: commandPaletteComponent
    }

    Component {
        id: commandPaletteComponent

        Item {
            id: paletteRoot
            opacity: 0
            property var filteredCommands: []
            property int selectedIndex: 0
            property string editingCommandId: ""
            property string paletteStatus: "↑↓ 选择，Enter 执行，F2 修改快捷键"

            Behavior on opacity {
                NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
            }

            function rebuild() {
                const needle = paletteQuery.text.trim().toLowerCase()
                const source = controller.commands
                const result = []
                for (let index = 0; index < source.length; ++index) {
                    const command = source[index]
                    const haystack = (command.title + " " + command.id + " "
                                      + command.category).toLowerCase()
                    if (needle.length === 0 || haystack.indexOf(needle) >= 0) {
                        result.push(command)
                    }
                }
                filteredCommands = result
                selectedIndex = Math.max(0, Math.min(selectedIndex, result.length - 1))
                commandList.currentIndex = selectedIndex
            }

            function activate() {
                opacity = 1
                rebuild()
                paletteQuery.forceActiveFocus()
                paletteQuery.selectAll()
            }

            function runSelected() {
                if (filteredCommands.length === 0) {
                    return
                }
                const command = filteredCommands[selectedIndex]
                root.closeCommandPalette()
                controller.executeCommand(command.id)
            }

            function beginShortcutEdit() {
                if (filteredCommands.length === 0) {
                    return
                }
                const command = filteredCommands[selectedIndex]
                editingCommandId = command.id
                shortcutEditor.text = command.shortcut
                shortcutEditorFrame.visible = true
                shortcutEditor.forceActiveFocus()
                shortcutEditor.selectAll()
                paletteStatus = "输入 PortableText 快捷键（例如 Ctrl+Alt+M），留空可禁用"
            }

            function saveShortcut() {
                if (controller.setShortcut(editingCommandId, shortcutEditor.text)) {
                    paletteStatus = "快捷键已保存"
                    shortcutEditorFrame.visible = false
                    editingCommandId = ""
                    rebuild()
                    paletteQuery.forceActiveFocus()
                } else {
                    paletteStatus = "快捷键无效或已被其他命令使用"
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#88000000"
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.closeCommandPalette()
                }
            }

            Rectangle {
                id: palettePanel
                x: Math.round((parent.width - width) / 2)
                y: Math.max(72, Math.round(parent.height * 0.14))
                width: Math.min(root.commandPaletteMaximumWidth, parent.width - 64)
                height: Math.min(500, parent.height - y - 60)
                radius: 6
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: 1

                Behavior on color {
                    ColorAnimation { duration: root.transitionDuration }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) { mouse.accepted = true }
                }

                Rectangle {
                    id: paletteQueryFrame
                    x: 14
                    y: 14
                    width: parent.width - 28
                    height: 40
                    radius: 4
                    color: root.themeFieldColor
                    border.color: paletteQuery.activeFocus
                                  ? root.panelAccentColor : root.themeBorderColor

                    TextInput {
                        id: paletteQuery
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.themeTextColor
                        selectionColor: root.panelAccentColor
                        selectedTextColor: root.panelAccentTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: 11
                        selectByMouse: true
                        clip: true
                        onTextChanged: paletteRoot.rebuild()

                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Down) {
                                paletteRoot.selectedIndex = Math.min(
                                    paletteRoot.filteredCommands.length - 1,
                                    paletteRoot.selectedIndex + 1
                                )
                                commandList.currentIndex = paletteRoot.selectedIndex
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up) {
                                paletteRoot.selectedIndex = Math.max(0, paletteRoot.selectedIndex - 1)
                                commandList.currentIndex = paletteRoot.selectedIndex
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                paletteRoot.runSelected()
                                event.accepted = true
                            } else if (event.key === Qt.Key_F2) {
                                paletteRoot.beginShortcutEdit()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                root.closeCommandPalette()
                                event.accepted = true
                            }
                        }
                    }
                }

                ListView {
                    id: commandList
                    x: 10
                    y: 66
                    width: parent.width - 20
                    height: parent.height - 118
                    clip: true
                    spacing: 2
                    model: paletteRoot.filteredCommands
                    currentIndex: paletteRoot.selectedIndex

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: commandList.width
                        height: 38
                        radius: 3
                        color: index === paletteRoot.selectedIndex
                               ? root.panelAccentColor : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: index === paletteRoot.selectedIndex
                                   ? root.panelAccentTextColor : root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: 10
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.shortcut
                            color: index === paletteRoot.selectedIndex
                                   ? root.panelAccentTextColor : root.themeMutedTextColor
                            font.family: "Cascadia Mono"
                            font.pointSize: 9
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                paletteRoot.selectedIndex = index
                                commandList.currentIndex = index
                            }
                            onClicked: paletteRoot.runSelected()
                        }
                    }
                }

                Text {
                    x: 16
                    y: parent.height - 38
                    width: parent.width - 32
                    text: paletteRoot.paletteStatus
                    color: root.themeMutedTextColor
                    elide: Text.ElideRight
                    font.family: root.uiFontFamily
                    font.pointSize: 8
                }

                Rectangle {
                    id: shortcutEditorFrame
                    visible: false
                    x: 14
                    y: parent.height - 86
                    width: parent.width - 28
                    height: 38
                    radius: 4
                    color: root.themeFieldColor
                    border.color: root.panelAccentColor

                    TextInput {
                        id: shortcutEditor
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.themeTextColor
                        selectionColor: root.panelAccentColor
                        selectedTextColor: root.panelAccentTextColor
                        font.family: "Cascadia Mono"
                        font.pointSize: 10
                        selectByMouse: true
                        Keys.onReturnPressed: paletteRoot.saveShortcut()
                        Keys.onEnterPressed: paletteRoot.saveShortcut()
                        Keys.onEscapePressed: {
                            shortcutEditorFrame.visible = false
                            paletteRoot.editingCommandId = ""
                            paletteQuery.forceActiveFocus()
                        }
                    }
                }

                Connections {
                    target: controller
                    function onCommandsChanged() { paletteRoot.rebuild() }
                }
            }
        }
    }

    MouseArea {
        z: 100
        width: root.resizeMargin
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: root.resizeMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.resizeMargin
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }

    MouseArea {
        z: 100
        width: root.resizeMargin
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.resizeMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.resizeMargin
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        z: 101
        height: root.resizeMargin
        anchors.left: parent.left
        anchors.leftMargin: root.resizeMargin
        anchors.right: parent.right
        anchors.rightMargin: root.resizeMargin
        anchors.top: parent.top
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.TopEdge)
    }

    MouseArea {
        z: 101
        height: root.resizeMargin
        anchors.left: parent.left
        anchors.leftMargin: root.resizeMargin
        anchors.right: parent.right
        anchors.rightMargin: root.resizeMargin
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }

    MouseArea {
        z: 102
        width: root.resizeMargin
        height: root.resizeMargin
        anchors.left: parent.left
        anchors.top: parent.top
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }

    MouseArea {
        z: 102
        width: root.resizeMargin
        height: root.resizeMargin
        anchors.right: parent.right
        anchors.top: parent.top
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }

    MouseArea {
        z: 102
        width: root.resizeMargin
        height: root.resizeMargin
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }

    MouseArea {
        z: 102
        width: root.resizeMargin
        height: root.resizeMargin
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
