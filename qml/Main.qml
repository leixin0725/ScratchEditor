import QtQuick

Window {
    id: root

    readonly property var uiConfig: controller.uiConfig

    width: uiConfig.window.defaultWidth
    height: uiConfig.window.defaultHeight
    minimumWidth: uiConfig.window.minimumWidth
    minimumHeight: uiConfig.window.minimumHeight
    visible: false
    color: themeBackgroundColor
    title: controller.externalFileMode && controller.externalFileName.length > 0
           ? controller.externalFileName + " — ScratchEditor"
           : "ScratchEditor"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    readonly property int dragZoneHeight: uiConfig.layout.dragZoneHeight
    readonly property int marginSize: uiConfig.layout.margin
    readonly property int resizeMargin: uiConfig.layout.resizeMargin
    readonly property int edgeDragWidth: marginSize - resizeMargin
    readonly property bool cornerResizeEnabled: true
    readonly property bool edgeDragEnabled: true
    readonly property bool verticalScrollBarVisible: scrollThumb.visible
    readonly property bool commandPaletteLoaded: commandPaletteLoader.active
    readonly property bool findPanelVisible: findPanel.visible
    readonly property bool settingsPageLoaded: settingsLoader.active
    readonly property bool settingsPageVisible: settingsLoader.active
    readonly property bool darkTheme: controller.theme !== "light"
    readonly property var uiThemeColors: uiConfig.palette[darkTheme ? "dark" : "light"]
    readonly property color themeBackgroundColor: uiThemeColors.background
    readonly property color themeEditorSurfaceColor: uiThemeColors.editorSurface
    readonly property color themeHeaderColor: themeBackgroundColor
    readonly property color themePanelColor: uiThemeColors.panel
    readonly property color themeFieldColor: uiThemeColors.field
    readonly property color themeTextColor: uiThemeColors.text
    readonly property color themeStrongTextColor: uiThemeColors.strongText
    readonly property color themeMutedTextColor: uiThemeColors.mutedText
    readonly property color themeBorderColor: uiThemeColors.border
    readonly property color themeButtonColor: uiThemeColors.button
    readonly property color themeAccentColor: controller.themeAccentColor
    readonly property color themeAccentTextColor: controller.themeAccentTextColor
    readonly property color themeFocusColor: themeAccentColor
    readonly property color themeSelectionColor: themeAccentColor
    readonly property color themeSelectedTextColor: themeAccentTextColor
    readonly property color selectionDragColor: themeAccentColor
    readonly property color themeDangerColor: uiThemeColors.danger
    readonly property color themeDangerTextColor: uiThemeColors.dangerText
    readonly property color themeButtonAccentTextColor: uiThemeColors.buttonAccentText
    readonly property color overlayColor: uiThemeColors.overlay
    readonly property color themeScrollbarThumbActiveColor: uiThemeColors.scrollbarThumbActive
    readonly property color themeScrollbarThumbIdleColor: uiThemeColors.scrollbarThumbIdle
    readonly property color panelAccentColor: themeAccentColor
    readonly property color panelAccentTextColor: themeAccentTextColor
    readonly property int commandPaletteMaximumWidth: uiConfig.panels.commandPalette.maxWidth
    readonly property color markdownTextColor: controller.markdownTextColor
    readonly property string uiFontFamily: uiConfig.fonts.family
    readonly property string uiMonospaceFontFamily: uiConfig.fonts.monospaceFamily
    readonly property int transitionDuration:
        controller.animationsEnabled ? uiConfig.animation.transitionDuration : 0
    readonly property int scrollAnimationDurationMs: controller.animationsEnabled ? 160 : 0
    readonly property int historyTriggerWidth: uiConfig.panels.history.triggerWidth
    readonly property int historyHoverOpenDelayMs: uiConfig.animation.historyHoverOpenDelay
    readonly property int historyHoverCloseDelayMs: uiConfig.animation.historyHoverCloseDelay
    property bool historyPanelOpen: false
    property bool historyPanelOpenedByCommand: false
    property bool historyRevealHovered: false
    property bool historyPanelHovered: false
    readonly property real historyPanelWidth:
        Math.max(uiConfig.panels.history.minWidth,
                 Math.min(uiConfig.panels.history.maxWidth, root.width / 3))
    readonly property bool historyPanelOverlay:
        (root.width - root.marginSize * 2 - historyPanelWidth)
        < uiConfig.panels.history.overlayThreshold
    readonly property real editorVisibleWidth:
        root.width - root.marginSize * 2
        - (historyPanelOpen && !historyPanelOverlay ? historyPanelWidth : 0)
    readonly property bool historyPanelLoaded:
        historyPanelLoader.active && historyPanelLoader.item !== null
    readonly property real historyPanelEdgeIntrusion:
        historyPanelLoader.item ? historyPanelLoader.item.edgeIntrusion : 0
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
    property bool inputScrollHoldBottom: false

    function scrollToBottom() {
        editorViewport.contentY = Math.max(0, editorViewport.contentHeight - editorViewport.height)
    }

    property real requestedScrollY: -1

    // PageUp/PageDown 与输入触底/删除触顶的自动滚动共用同一个动画入口：
    // 动画开启时从当前位置平滑滑到目标，关闭时直接落位。
    function animateScrollTo() {
        const targetY = requestedScrollY
        const maximumY = Math.max(0, editorViewport.contentHeight - editorViewport.height)
        const clampedY = Math.max(0, Math.min(maximumY, targetY))
        scrollAnimation.stop()
        if (root.scrollAnimationDurationMs <= 0
                || Math.abs(clampedY - editorViewport.contentY) < 0.5) {
            editorViewport.contentY = clampedY
            return
        }
        scrollAnimation.from = editorViewport.contentY
        scrollAnimation.to = clampedY
        scrollAnimation.start()
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
        interval: uiConfig.animation.scrollRefreshInterval
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
            font.pointSize: uiConfig.fonts.heading
            font.weight: Font.DemiBold
        }

        Text {
            id: statusText
            anchors.right: parent.right
            anchors.rightMargin: root.marginSize - root.resizeMargin
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(uiConfig.panels.statusText.maxWidth,
                            parent.width * uiConfig.panels.statusText.maxWidthRatio)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: controller.statusHealthy ? controller.statusPanelSummary : controller.statusMessage
            color: controller.statusHealthy ? root.themeMutedTextColor : root.themeDangerColor
            font.family: root.uiFontFamily
            font.pointSize: uiConfig.fonts.small

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
            anchors.topMargin: uiConfig.panels.statusPanel.topGap
            anchors.right: statusText.right
            width: Math.min(controller.statusPanelMaxWidth,
                            header.width - statusText.x - uiConfig.panels.statusPanel.topGap)
            height: Math.min(contentHeight,
                             root.height - y - uiConfig.panels.statusPanel.bottomGap)
            radius: uiConfig.layout.radiusMedium
            color: root.themePanelColor
            border.color: root.themeBorderColor
            border.width: uiConfig.layout.borderWidth
            visible: root.statusPanelOpen
            opacity: root.statusPanelOpen ? 1 : 0
            clip: true

            property real contentHeight: (controller.statusHealthy
                                          ? normalColumn.implicitHeight
                                          : errorText.implicitHeight)
                                         + uiConfig.panels.statusPanel.padding

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
                anchors.margins: uiConfig.panels.statusPanel.margins
                spacing: uiConfig.panels.statusPanel.spacing
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
                    anchors.margins: uiConfig.panels.statusPanel.margins
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
        y: root.dragZoneHeight + uiConfig.panels.find.gap
        width: Math.min(uiConfig.panels.find.maxWidth,
                        root.width - uiConfig.panels.find.widthInset)
        height: root.replaceMode ? uiConfig.panels.find.heightReplace
                                 : uiConfig.panels.find.heightSingle
        radius: uiConfig.layout.radiusNormal
        color: root.themePanelColor
        border.color: root.themeBorderColor
        border.width: uiConfig.layout.borderWidth
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: root.transitionDuration; easing.type: Easing.OutCubic }
        }

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }

        Rectangle {
            id: findFieldFrame
            x: uiConfig.panels.find.paddingX
            y: uiConfig.panels.find.paddingY
            width: findPanel.width - uiConfig.panels.find.controlsWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeFieldColor
            border.color: findInput.activeFocus ? root.themeFocusColor : root.themeBorderColor

            TextInput {
                id: findInput
                anchors.fill: parent
                anchors.leftMargin: uiConfig.layout.spacingInput
                anchors.rightMargin: uiConfig.layout.spacingInput
                verticalAlignment: TextInput.AlignVCenter
                color: root.themeTextColor
                selectionColor: root.themeSelectionColor
                selectedTextColor: root.themeSelectedTextColor
                font.family: root.uiFontFamily
                font.pointSize: uiConfig.fonts.normal
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
            x: findFieldFrame.x + findFieldFrame.width + uiConfig.panels.find.gap
            y: uiConfig.panels.find.paddingY
            width: uiConfig.panels.find.caseSensitiveWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: enabledValue ? root.themeAccentColor : root.themeButtonColor

            Text {
                anchors.centerIn: parent
                text: "Aa"
                color: root.themeStrongTextColor
                font.family: root.uiMonospaceFontFamily
                font.pointSize: uiConfig.fonts.small
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: caseSensitiveToggle.enabledValue = !caseSensitiveToggle.enabledValue
            }
        }

        Rectangle {
            id: findPrevButton
            x: caseSensitiveToggle.x + caseSensitiveToggle.width
               + uiConfig.panels.find.gap
            y: uiConfig.panels.find.paddingY
            width: uiConfig.panels.find.prevWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeButtonColor
            Text {
                anchors.centerIn: parent
                text: "上一个"
                color: root.themeTextColor
                font.pointSize: uiConfig.fonts.small
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.findInDocument(true)
            }
        }

        Rectangle {
            id: findNextButton
            x: findPrevButton.x + findPrevButton.width + uiConfig.panels.find.gap
            y: uiConfig.panels.find.paddingY
            width: uiConfig.panels.find.nextWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeAccentColor
            Text {
                anchors.centerIn: parent
                text: "下一个"
                color: root.themeButtonAccentTextColor
                font.pointSize: uiConfig.fonts.small
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.findInDocument(false)
            }
        }

        Rectangle {
            id: findCloseButton
            x: findPanel.width - uiConfig.panels.find.rightInset
            y: uiConfig.panels.find.paddingY
            width: uiConfig.panels.find.closeWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeButtonColor
            Text {
                anchors.centerIn: parent
                text: "×"
                color: root.themeTextColor
                font.pointSize: uiConfig.fonts.title
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.hideFindPanel()
            }
        }

        Rectangle {
            id: replaceFieldFrame
            visible: root.replaceMode
            x: uiConfig.panels.find.paddingX
            y: uiConfig.panels.find.paddingY + uiConfig.layout.controlHeightSmall
               + uiConfig.panels.find.rowGap
            width: findFieldFrame.width
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeFieldColor
            border.color: replaceInput.activeFocus ? root.themeFocusColor : root.themeBorderColor

            TextInput {
                id: replaceInput
                anchors.fill: parent
                anchors.leftMargin: uiConfig.layout.spacingInput
                anchors.rightMargin: uiConfig.layout.spacingInput
                verticalAlignment: TextInput.AlignVCenter
                color: root.themeTextColor
                selectionColor: root.themeSelectionColor
                selectedTextColor: root.themeSelectedTextColor
                font.family: root.uiFontFamily
                font.pointSize: uiConfig.fonts.normal
                selectByMouse: true
                clip: true
                Keys.onEscapePressed: root.hideFindPanel()
            }
        }

        Rectangle {
            id: replaceCurrentButton
            visible: root.replaceMode
            x: replaceFieldFrame.x + replaceFieldFrame.width + uiConfig.panels.find.gap
            y: replaceFieldFrame.y
            width: uiConfig.panels.find.actionWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeButtonColor
            Text {
                anchors.centerIn: parent
                text: "替换当前"
                color: root.themeTextColor
                font.pointSize: uiConfig.fonts.small
            }
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
            id: replaceAllButton
            visible: root.replaceMode
            x: replaceCurrentButton.x + replaceCurrentButton.width
               + uiConfig.panels.find.gap
            y: replaceFieldFrame.y
            width: uiConfig.panels.find.actionWidth
            height: uiConfig.layout.controlHeightSmall
            radius: uiConfig.layout.radiusSmall
            color: root.themeAccentColor
            Text {
                anchors.centerIn: parent
                text: "全部替换"
                color: root.themeButtonAccentTextColor
                font.pointSize: uiConfig.fonts.small
            }
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
            x: findPanel.width - uiConfig.panels.find.statusWidth
               - uiConfig.panels.find.paddingX
            y: root.replaceMode ? uiConfig.panels.find.statusYReplace
                                : uiConfig.panels.find.statusY
            width: uiConfig.panels.find.statusWidth
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: root.searchStatus
            color: root.themeMutedTextColor
            font.family: root.uiFontFamily
            font.pointSize: uiConfig.fonts.caption
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
        // 删除触顶保持期间开启弹性底部缓冲：contentHeight 不小于
        // contentY + 视口高，避免内容收缩时 Flickable 把视口钳制回新 max，
        // 让光标随删除自然上移、再次触顶才触发镜像滚动。
        // editorContentBottomGap 作为最小留白值参与计算（默认远小于 2/3 页）。
        contentHeight: Math.max(
            height,
            editor.contentHeight + (root.inputScrollHoldBottom
                ? Math.max(Math.max(height * 2 / 3, uiConfig.layout.editorContentBottomGap),
                           editorViewport.contentY + height - editor.contentHeight)
                : Math.max(height * 2 / 3, uiConfig.layout.editorContentBottomGap)))
        pixelAligned: true

        Behavior on x { NumberAnimation { duration: root.transitionDuration } }
        Behavior on width { NumberAnimation { duration: root.transitionDuration } }

        NumberAnimation {
            id: scrollAnimation
            target: editorViewport
            property: "contentY"
            duration: root.scrollAnimationDurationMs
            easing.type: Easing.OutCubic
        }

        // 用户拖动/甩动时立即停止动画，避免与手势互相打架；
        // 同时释放删除/撤销保持期间的弹性底部缓冲，手动滚动后由下一次检查重新判定。
        onDragStarted: {
            scrollAnimation.stop()
            root.inputScrollHoldBottom = false
        }
        onFlickStarted: {
            scrollAnimation.stop()
            root.inputScrollHoldBottom = false
        }

        // 滚轮手动滚动同样释放弹性缓冲；target 为 null 时不自动操纵对象，
        // blocking 为 false 时不吞掉滚轮事件，Flickable 内置滚轮滚动不受影响。
        WheelHandler {
            target: null
            blocking: false
            onWheel: root.inputScrollHoldBottom = false
        }

        TextEdit {
            id: editor
            objectName: "scratchText"
            property int selectionDragPosition: -1
            readonly property rect selectionDragRectangle:
                selectionDragPosition >= 0
                    ? positionToRectangle(selectionDragPosition)
                    : Qt.rect(0, 0, 0, 0)
            x: uiConfig.layout.editorPaddingX
            y: uiConfig.layout.editorPaddingY
            width: editorViewport.width - uiConfig.layout.editorPaddingX * 2
            height: Math.max(
                editorViewport.height - uiConfig.layout.editorPaddingY * 2, contentHeight)
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
                width: uiConfig.layout.selectionCursorWidth
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
            readonly property real edgeIntrusion:
                root.historyPanelOpen ? 0
                                      : Math.max(0, historyPanel.x + historyPanel.width)

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
                // 闭合时右边缘正好压在 loader 左边界（左边框与编辑区域交界）。
                // 滑动进度只在开/合切换时变化并带动画；x 直接由宽度与进度绑定，
                // 窗口缩放期间宽度变化会让闭合 x 即时跟随，避免 Behavior 逐帧
                // 重启动画造成右边缘短暂探入可见裁剪区（唤出窗口时闪现）。
                property real slideProgress: root.historyPanelOpen ? 1 : 0
                x: -historyPanel.width + historyPanel.width * slideProgress
                width: root.historyPanelWidth
                height: parent.height
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: uiConfig.layout.borderWidth

                Behavior on slideProgress {
                    NumberAnimation {
                        duration: root.transitionDuration
                        easing.type: Easing.OutCubic
                    }
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
                    x: uiConfig.panels.history.titleX
                    y: uiConfig.panels.history.titleY
                    text: "剪贴板历史"
                    color: root.themeStrongTextColor
                    font.family: root.uiFontFamily
                    font.pointSize: uiConfig.fonts.heading
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    id: historySearchFrame
                    x: uiConfig.panels.history.searchX
                    y: uiConfig.panels.history.searchY
                    width: parent.width - uiConfig.panels.history.searchInsetX
                    height: uiConfig.layout.controlHeightNormal
                    radius: uiConfig.layout.radiusNormal
                    color: root.themeFieldColor
                    border.color: historyQuery.activeFocus
                                  ? root.themeFocusColor : root.themeBorderColor

                    TextInput {
                        id: historyQuery
                        anchors.fill: parent
                        anchors.margins: uiConfig.panels.history.inputMargin
                        color: root.themeTextColor
                        selectionColor: root.themeSelectionColor
                        selectedTextColor: root.themeSelectedTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: uiConfig.fonts.small
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
                        anchors.leftMargin: uiConfig.panels.history.inputMargin
                        anchors.verticalCenter: parent.verticalCenter
                        visible: historyQuery.text.length === 0
                        text: "搜索完整文本"
                        color: root.themeMutedTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: uiConfig.fonts.small
                    }
                }

                ListView {
                    id: historyList
                    x: uiConfig.panels.history.listX
                    y: uiConfig.panels.history.listY
                    width: parent.width - uiConfig.panels.history.listInsetX
                    height: parent.height - uiConfig.panels.history.listBottomInset
                    clip: true
                    spacing: uiConfig.layout.spacingSmall
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
                        radius: uiConfig.layout.radiusNormal
                        color: root.historySelectedId === historyId
                               ? root.themeButtonColor : "transparent"
                        border.color: root.historySelectedId === historyId
                                      ? root.themeAccentColor : "transparent"

                        Text {
                            x: uiConfig.panels.history.cardTextX
                            y: uiConfig.panels.history.cardTextY
                            width: parent.width - uiConfig.panels.history.cardTextInsetX
                            height: parent.height - uiConfig.panels.history.cardMetaHeight
                            text: historyRow.previewText
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.small
                            wrapMode: Text.Wrap
                            maximumLineCount: Math.max(
                                1, Math.min(uiConfig.panels.history.cardMaxLines,
                                            Math.floor((parent.height
                                                        - uiConfig.panels.history.cardMetaHeight)
                                                       / uiConfig.panels.history.cardLineHeight)))
                            elide: Text.ElideRight
                        }
                        Text {
                            x: uiConfig.panels.history.cardTextX
                            y: parent.height - uiConfig.panels.history.cardMetaBottomGap
                            width: parent.width - uiConfig.panels.history.cardTextInsetX
                            text: new Date(historyRow.capturedAtMs).toLocaleString(
                                      Qt.locale(), Locale.ShortFormat)
                                  + " · " + historyRow.characterCount + " 字"
                            color: root.themeMutedTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.caption
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
                    width: historyList.width - uiConfig.panels.history.emptyTextInsetX
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.uiFontFamily
                    font.pointSize: uiConfig.fonts.small
                }

                Rectangle {
                    x: uiConfig.panels.history.footerMarginX
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: uiConfig.panels.history.footerBottomGap
                    width: uiConfig.panels.history.footerButtonWidth
                    height: uiConfig.layout.controlHeightCompact
                    radius: uiConfig.layout.radiusNormal
                    color: root.themeButtonColor
                    Text {
                        anchors.centerIn: parent
                        text: "删除"
                        color: root.themeTextColor
                        font.pointSize: uiConfig.fonts.small
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.historySelectedId.length > 0
                        onClicked: controller.deleteClipboardHistoryItem(root.historySelectedId)
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: uiConfig.panels.history.footerMarginX
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: uiConfig.panels.history.footerBottomGap
                    width: uiConfig.panels.history.footerButtonWidth
                    height: uiConfig.layout.controlHeightCompact
                    radius: uiConfig.layout.radiusNormal
                    color: root.themeButtonColor
                    Text {
                        anchors.centerIn: parent
                        text: "清空"
                        color: root.themeDangerColor
                        font.pointSize: uiConfig.fonts.small
                    }
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
        color: root.overlayColor
        Rectangle {
            id: loadConfirmationPanel
            anchors.centerIn: parent
            width: Math.min(uiConfig.panels.dialog.maxWidth,
                            parent.width - uiConfig.panels.dialog.widthInset)
            height: uiConfig.panels.dialog.height
            radius: uiConfig.layout.radiusLarge
            color: root.themePanelColor
            border.color: root.themeBorderColor
            // 0 = 取消，1 = 载入；默认焦点在确认按钮。
            property int focusedButton: 1
            focus: true

            onVisibleChanged: {
                if (visible) {
                    focusedButton = 1
                    forceActiveFocus()
                } else if (historyPanelLoader.item) {
                    // 对话框关闭后把焦点还给历史面板搜索框。
                    historyPanelLoader.item.focusQuery()
                }
            }

            function activateFocused() {
                if (focusedButton === 1) {
                    controller.confirmLoadClipboardHistory()
                } else {
                    controller.cancelLoadClipboardHistory()
                }
            }

            Keys.onLeftPressed: function(event) {
                focusedButton = focusedButton === 1 ? 0 : 1
                event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                focusedButton = focusedButton === 1 ? 0 : 1
                event.accepted = true
            }
            Keys.onReturnPressed: function(event) { activateFocused(); event.accepted = true }
            Keys.onEnterPressed: function(event) { activateFocused(); event.accepted = true }
            Keys.onSpacePressed: function(event) { activateFocused(); event.accepted = true }

            Text {
                anchors.top: parent.top
                anchors.topMargin: uiConfig.panels.dialog.titleTop
                anchors.horizontalCenter: parent.horizontalCenter
                text: "放弃当前修改并载入历史？"
                color: root.themeTextColor
                font.family: root.uiFontFamily
                font.pointSize: uiConfig.fonts.dialogTitle
            }
            Rectangle {
                x: uiConfig.panels.dialog.buttonSide
                y: uiConfig.panels.dialog.buttonY
                width: uiConfig.panels.dialog.buttonWidth
                height: uiConfig.layout.controlHeightNormal
                radius: uiConfig.layout.radiusNormal
                color: loadConfirmationPanel.focusedButton === 0
                       ? root.themeAccentColor : root.themeButtonColor
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    color: loadConfirmationPanel.focusedButton === 0
                           ? root.panelAccentTextColor : root.themeTextColor
                }
                MouseArea { anchors.fill: parent; onClicked: controller.cancelLoadClipboardHistory() }
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: uiConfig.panels.dialog.buttonSide
                y: uiConfig.panels.dialog.buttonY
                width: uiConfig.panels.dialog.buttonWidth
                height: uiConfig.layout.controlHeightNormal
                radius: uiConfig.layout.radiusNormal
                color: loadConfirmationPanel.focusedButton === 1
                       ? root.themeAccentColor : root.themeButtonColor
                Text {
                    anchors.centerIn: parent
                    text: "载入"
                    color: loadConfirmationPanel.focusedButton === 1
                           ? root.panelAccentTextColor : root.themeTextColor
                }
                MouseArea { anchors.fill: parent; onClicked: controller.confirmLoadClipboardHistory() }
            }
        }
    }

    Rectangle {
        z: 95
        anchors.fill: parent
        visible: controller.historyClearConfirmationVisible
        color: root.overlayColor
        Rectangle {
            id: clearConfirmationPanel
            anchors.centerIn: parent
            width: Math.min(uiConfig.panels.dialog.maxWidth,
                            parent.width - uiConfig.panels.dialog.widthInset)
            height: uiConfig.panels.dialog.height
            radius: uiConfig.layout.radiusLarge
            color: root.themePanelColor
            border.color: root.themeBorderColor
            // 0 = 取消，1 = 清空；默认焦点在确认按钮。
            property int focusedButton: 1
            focus: true

            onVisibleChanged: {
                if (visible) {
                    focusedButton = 1
                    forceActiveFocus()
                } else if (historyPanelLoader.item) {
                    // 对话框关闭后把焦点还给历史面板搜索框。
                    historyPanelLoader.item.focusQuery()
                }
            }

            function activateFocused() {
                if (focusedButton === 1) {
                    controller.confirmClearClipboardHistory()
                } else {
                    controller.cancelClearClipboardHistory()
                }
            }

            Keys.onLeftPressed: function(event) {
                focusedButton = focusedButton === 1 ? 0 : 1
                event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                focusedButton = focusedButton === 1 ? 0 : 1
                event.accepted = true
            }
            Keys.onReturnPressed: function(event) { activateFocused(); event.accepted = true }
            Keys.onEnterPressed: function(event) { activateFocused(); event.accepted = true }
            Keys.onSpacePressed: function(event) { activateFocused(); event.accepted = true }

            Text {
                anchors.top: parent.top
                anchors.topMargin: uiConfig.panels.dialog.titleTop
                anchors.horizontalCenter: parent.horizontalCenter
                text: "确认清空全部剪贴板历史？"
                color: root.themeTextColor
                font.family: root.uiFontFamily
                font.pointSize: uiConfig.fonts.dialogTitle
            }
            Rectangle {
                x: uiConfig.panels.dialog.buttonSide
                y: uiConfig.panels.dialog.buttonY
                width: uiConfig.panels.dialog.buttonWidth
                height: uiConfig.layout.controlHeightNormal
                radius: uiConfig.layout.radiusNormal
                color: clearConfirmationPanel.focusedButton === 0
                       ? root.themeAccentColor : root.themeButtonColor
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    color: clearConfirmationPanel.focusedButton === 0
                           ? root.panelAccentTextColor : root.themeTextColor
                }
                MouseArea { anchors.fill: parent; onClicked: controller.cancelClearClipboardHistory() }
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: uiConfig.panels.dialog.buttonSide
                y: uiConfig.panels.dialog.buttonY
                width: uiConfig.panels.dialog.buttonWidth
                height: uiConfig.layout.controlHeightNormal
                radius: uiConfig.layout.radiusNormal
                color: clearConfirmationPanel.focusedButton === 1
                       ? root.themeAccentColor : root.themeDangerColor
                Text {
                    anchors.centerIn: parent
                    text: "清空"
                    color: clearConfirmationPanel.focusedButton === 1
                           ? root.panelAccentTextColor : root.themeDangerTextColor
                }
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
        x: root.width - root.marginSize + uiConfig.layout.scrollbarOffset
        y: editorViewport.y
            + (editorViewport.contentY
               / Math.max(1, editorViewport.contentHeight - editorViewport.height))
              * Math.max(0, editorViewport.height - height)
        width: uiConfig.layout.scrollbarWidth
        height: Math.max(
            uiConfig.layout.scrollbarMinHeight,
            editorViewport.height * editorViewport.height
                / Math.max(editorViewport.height, root.scrollContentHeight)
        )
        visible: root.scrollContentHeight > editorViewport.height + 0.5
        color: scrollHover.hovered || editorViewport.movingVertically
               ? root.themeScrollbarThumbActiveColor
               : root.themeScrollbarThumbIdleColor

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
                scrollAnimation.stop()
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
        y: root.dragZoneHeight - uiConfig.animation.probeYOffset
        width: uiConfig.animation.probeWidth
        height: uiConfig.animation.probeHeight
        radius: uiConfig.animation.probeRadius
        color: root.themeAccentColor
    }

    NumberAnimation {
        id: benchmarkAnimation
        target: animationProbe
        from: root.marginSize
        to: Math.max(root.marginSize, root.width - root.marginSize - animationProbe.width)
        duration: uiConfig.animation.benchmarkDuration
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
            property string draftTheme: uiConfig.preferences.theme
            property string draftFontFamily: ""
            property int draftFontPointSize: uiConfig.fonts.editorDefaultSize
            property bool draftAnimationsEnabled: uiConfig.preferences.animationsEnabled
            property int draftStatusPanelFontSize: uiConfig.panels.statusPanel.defaultFontSize
            property int draftStatusPanelShowDelayMs:
                uiConfig.panels.statusPanel.defaultShowDelayMs
            property int draftStatusPanelHideDelayMs:
                uiConfig.panels.statusPanel.defaultHideDelayMs
            property int draftStatusPanelMaxWidth:
                uiConfig.panels.statusPanel.defaultMaxWidth
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
                color: root.overlayColor
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.closeSettings()
                }
            }

            Rectangle {
                id: settingsPanel
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: Math.min(uiConfig.panels.settingsPage.maxWidth,
                                parent.width - uiConfig.panels.settingsPage.widthInset)
                height: Math.min(uiConfig.panels.settingsPage.maxHeight,
                                 parent.height - uiConfig.panels.settingsPage.heightInset)
                radius: uiConfig.layout.radiusXLarge
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: uiConfig.layout.borderWidth

                Behavior on color {
                    ColorAnimation { duration: root.transitionDuration }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) { mouse.accepted = true }
                }

                Text {
                    x: uiConfig.panels.settingsPage.paddingX
                    y: uiConfig.panels.settingsPage.titleY
                    text: "设置"
                    color: root.themeStrongTextColor
                    font.family: root.uiFontFamily
                    font.pointSize: uiConfig.fonts.title
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: uiConfig.panels.settingsPage.closeMarginX
                    y: uiConfig.panels.settingsPage.titleY
                    text: "×"
                    color: root.themeMutedTextColor
                    font.pointSize: uiConfig.fonts.title
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: uiConfig.panels.settingsPage.closeHitInset
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeSettings()
                    }
                }

                Flickable {
                    id: settingsContent
                    x: uiConfig.panels.settingsPage.paddingX
                    y: uiConfig.panels.settingsPage.contentY
                    width: parent.width - uiConfig.panels.settingsPage.paddingX * 2
                    height: parent.height - uiConfig.panels.settingsPage.contentBottomInset
                    contentWidth: width
                    contentHeight: uiConfig.panels.settingsPage.contentHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Item {
                        width: settingsContent.width
                        height: settingsContent.contentHeight

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 0
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "主题"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 0
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                            x: uiConfig.panels.settingsPage.columnX
                               + uiConfig.panels.settingsPage.controlWidth
                               + uiConfig.layout.spacingMedium
                            y: uiConfig.panels.settingsPage.rowHeight * 0
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                            y: uiConfig.panels.settingsPage.rowHeight * 1
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "编辑字体"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 1
                            width: parent.width - uiConfig.panels.settingsPage.columnX
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: root.themeFieldColor
                            border.color: fontFamilyInput.activeFocus
                                          ? root.panelAccentColor : root.themeBorderColor
                            TextInput {
                                id: fontFamilyInput
                                anchors.fill: parent
                                anchors.leftMargin: uiConfig.layout.spacingLarge
                                anchors.rightMargin: uiConfig.layout.spacingLarge
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.themeTextColor
                                selectionColor: root.panelAccentColor
                                selectedTextColor: root.panelAccentTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                selectByMouse: true
                                clip: true
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 2
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "字号（" + uiConfig.fonts.editorSizeMin
                                  + "–" + uiConfig.fonts.editorSizeMax + "）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 2
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.fonts.editorSizeMin
                                    top: uiConfig.fonts.editorSizeMax
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 3
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "轻量动画"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 3
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusPill
                            color: settingsRoot.draftAnimationsEnabled
                                   ? root.panelAccentColor : root.themeButtonColor
                            border.color: root.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: settingsRoot.draftAnimationsEnabled ? "开启" : "关闭"
                                color: settingsRoot.draftAnimationsEnabled
                                       ? root.panelAccentTextColor : root.themeTextColor
                                font.family: root.uiFontFamily
                                font.pointSize: uiConfig.fonts.small
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
                            y: uiConfig.panels.settingsPage.rowHeight * 4
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "面板字号（" + uiConfig.panels.statusPanel.fontSizeMin
                                  + "–" + uiConfig.panels.statusPanel.fontSizeMax + "）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 4
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.fontSizeMin
                                    top: uiConfig.panels.statusPanel.fontSizeMax
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 5
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "显示延迟（毫秒）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 5
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.showDelayMinMs
                                    top: uiConfig.panels.statusPanel.showDelayMaxMs
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 6
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "收起延迟（毫秒）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 6
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.hideDelayMinMs
                                    top: uiConfig.panels.statusPanel.hideDelayMaxMs
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 7
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "最大宽度（像素）"
                            color: root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 7
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
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
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.maxWidthMin
                                    top: uiConfig.panels.statusPanel.maxWidthMax
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.contentHeight
                               - uiConfig.panels.settingsPage.captionTopGap
                            text: "集中配置文件"
                            color: root.themeMutedTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.caption
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.contentHeight
                               - uiConfig.panels.settingsPage.captionBottomGap
                            width: parent.width
                            text: controller.settingsFile
                            color: root.themeMutedTextColor
                            font.family: root.uiMonospaceFontFamily
                            font.pointSize: uiConfig.fonts.caption
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Text {
                    x: uiConfig.panels.settingsPage.paddingX
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: uiConfig.panels.settingsPage.saveStatusBottomGap
                    width: parent.width - uiConfig.panels.settingsPage.saveStatusWidthInset
                    text: settingsRoot.saveStatus
                    color: settingsRoot.saveStatus === "设置已保存"
                           ? root.themeMutedTextColor : root.themeDangerColor
                    font.family: root.uiFontFamily
                    font.pointSize: uiConfig.fonts.small
                    elide: Text.ElideRight
                }

                Rectangle {
                    anchors.right: applySettingsButton.left
                    anchors.rightMargin: uiConfig.panels.settingsPage.buttonsGap
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: uiConfig.panels.settingsPage.buttonsBottomGap
                    width: uiConfig.panels.settingsPage.actionButtonWidth
                    height: uiConfig.layout.controlHeightLarge
                    radius: uiConfig.layout.radiusNormal
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
                    anchors.rightMargin: uiConfig.panels.settingsPage.applyRightMargin
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: uiConfig.panels.settingsPage.buttonsBottomGap
                    width: uiConfig.panels.settingsPage.actionButtonWidth
                    height: uiConfig.layout.controlHeightLarge
                    radius: uiConfig.layout.radiusNormal
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
                color: root.overlayColor
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.closeCommandPalette()
                }
            }

            Rectangle {
                id: palettePanel
                x: Math.round((parent.width - width) / 2)
                y: Math.max(uiConfig.panels.commandPalette.minTop,
                            Math.round(parent.height * uiConfig.panels.commandPalette.topRatio))
                width: Math.min(root.commandPaletteMaximumWidth,
                                parent.width - uiConfig.panels.commandPalette.widthInset)
                height: Math.min(uiConfig.panels.commandPalette.maxHeight,
                                 parent.height - y - uiConfig.panels.commandPalette.bottomGap)
                radius: uiConfig.layout.radiusLarge
                color: root.themePanelColor
                border.color: root.themeBorderColor
                border.width: uiConfig.layout.borderWidth

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
                    x: uiConfig.panels.commandPalette.innerPaddingX
                    y: uiConfig.panels.commandPalette.innerPaddingY
                    width: parent.width - uiConfig.panels.commandPalette.insetX
                    height: uiConfig.layout.controlHeightExtraTall
                    radius: uiConfig.layout.radiusNormal
                    color: root.themeFieldColor
                    border.color: paletteQuery.activeFocus
                                  ? root.panelAccentColor : root.themeBorderColor

                    TextInput {
                        id: paletteQuery
                        anchors.fill: parent
                        anchors.leftMargin: uiConfig.panels.commandPalette.textMarginX
                        anchors.rightMargin: uiConfig.panels.commandPalette.textMarginX
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.themeTextColor
                        selectionColor: root.panelAccentColor
                        selectedTextColor: root.panelAccentTextColor
                        font.family: root.uiFontFamily
                        font.pointSize: uiConfig.fonts.heading
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
                    x: uiConfig.panels.commandPalette.listX
                    y: uiConfig.panels.commandPalette.innerPaddingY
                       + uiConfig.layout.controlHeightExtraTall
                       + uiConfig.panels.commandPalette.listTopGap
                    width: parent.width - uiConfig.panels.commandPalette.listInsetX
                    height: parent.height - uiConfig.panels.commandPalette.listBottomInset
                    clip: true
                    spacing: uiConfig.layout.spacingTight
                    model: paletteRoot.filteredCommands
                    currentIndex: paletteRoot.selectedIndex

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: commandList.width
                        height: uiConfig.layout.controlHeightTall
                        radius: uiConfig.layout.radiusSmall
                        color: index === paletteRoot.selectedIndex
                               ? root.panelAccentColor : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: uiConfig.panels.commandPalette.rowTextMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: index === paletteRoot.selectedIndex
                                   ? root.panelAccentTextColor : root.themeTextColor
                            font.family: root.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: uiConfig.panels.commandPalette.rowTextMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.shortcut
                            color: index === paletteRoot.selectedIndex
                                   ? root.panelAccentTextColor : root.themeMutedTextColor
                            font.family: root.uiMonospaceFontFamily
                            font.pointSize: uiConfig.fonts.small
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
                    x: uiConfig.panels.commandPalette.statusX
                    y: parent.height - uiConfig.panels.commandPalette.statusYFromBottom
                    width: parent.width - uiConfig.panels.commandPalette.statusInsetX
                    text: paletteRoot.paletteStatus
                    color: root.themeMutedTextColor
                    elide: Text.ElideRight
                    font.family: root.uiFontFamily
                    font.pointSize: uiConfig.fonts.caption
                }

                Rectangle {
                    id: shortcutEditorFrame
                    visible: false
                    x: uiConfig.panels.commandPalette.innerPaddingX
                    y: parent.height - uiConfig.panels.commandPalette.shortcutYFromBottom
                    width: parent.width - uiConfig.panels.commandPalette.insetX
                    height: uiConfig.layout.controlHeightTall
                    radius: uiConfig.layout.radiusNormal
                    color: root.themeFieldColor
                    border.color: root.panelAccentColor

                    TextInput {
                        id: shortcutEditor
                        anchors.fill: parent
                        anchors.leftMargin: uiConfig.panels.commandPalette.rowTextMargin
                        anchors.rightMargin: uiConfig.panels.commandPalette.rowTextMargin
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.themeTextColor
                        selectionColor: root.panelAccentColor
                        selectedTextColor: root.panelAccentTextColor
                        font.family: root.uiMonospaceFontFamily
                        font.pointSize: uiConfig.fonts.normal
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
