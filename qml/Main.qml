import QtQuick

Window {
    id: root

    function clickHeadingFoldMarkerForTest(position) {
        for (let index = 0; index < headingFoldRepeater.count; ++index) {
            const marker = headingFoldRepeater.itemAt(index)
            if (marker && marker.modelData.position === position) {
                marker.activate()
                return true
            }
        }
        return false
    }

    function buildHeadingNavigationHighlightRectangles() {
        const target = controller.headingNavigationHighlight
        if (!target || target.start === undefined || target.end === undefined
                || target.end <= target.start) {
            return []
        }

        const rectangles = []
        let segmentStart = editor.positionToRectangle(target.start)
        let segmentY = segmentStart.y
        for (let position = target.start + 1; position <= target.end; ++position) {
            const cursorRect = editor.positionToRectangle(position)
            if (Math.abs(cursorRect.y - segmentY) <= 0.5) {
                if (position === target.end) {
                    rectangles.push({
                        "x": Math.min(segmentStart.x, cursorRect.x),
                        "y": segmentStart.y,
                        "width": Math.max(1, Math.abs(cursorRect.x - segmentStart.x)),
                        "height": Math.max(1, segmentStart.height)
                    })
                }
                continue
            }

            rectangles.push({
                "x": segmentStart.x,
                "y": segmentStart.y,
                "width": Math.max(1, editor.width - segmentStart.x),
                "height": Math.max(1, segmentStart.height)
            })
            if (position === target.end) {
                break
            }
            segmentStart = cursorRect
            segmentY = cursorRect.y
        }
        return rectangles
    }

    function refreshHeadingNavigationHighlightGeometry() {
        if (headingNavigationHighlightAnimationOpacity <= 0) {
            return
        }
        headingNavigationHighlightRectangles =
            buildHeadingNavigationHighlightRectangles()
    }

    function restartHeadingNavigationHighlight() {
        headingNavigationHighlightAnimation.stop()
        headingNavigationHighlightRectangles =
            buildHeadingNavigationHighlightRectangles()
        headingNavigationHighlightAnimationOpacity =
            headingNavigationHighlightRectangles.length > 0 ? 1 : 0
        if (headingNavigationHighlightRectangles.length > 0) {
            headingNavigationHighlightAnimation.start()
        }
    }

    function headingFoldMarkerStateForTest(position) {
        for (let index = 0; index < headingFoldRepeater.count; ++index) {
            const marker = headingFoldRepeater.itemAt(index)
            if (marker && marker.modelData.position === position) {
                const currentHeadingRectangle = editor.positionToRectangle(position)
                const gutterSceneX = editorViewport.x + headingFoldGutter.x
                const gutterSceneY = editorViewport.y - editorViewport.contentY
                    + headingFoldGutter.y
                return {
                    "iconName": marker.iconName,
                    "iconValid": marker.iconValid,
                    "iconSize": marker.iconSize,
                    "x": gutterSceneX + marker.x,
                    "y": gutterSceneY + marker.y,
                    "expectedX": gutterSceneX,
                    "expectedY": gutterSceneY + currentHeadingRectangle.y
                }
            }
        }
        return {}
    }

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
    readonly property real headerTitleLeft: marginSize
    readonly property real headerTitleCenterY:
        resizeMargin + (dragZoneHeight - resizeMargin) / 2
    readonly property real editorContentTop: dragZoneHeight
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
    readonly property int headingFoldGutterWidth: uiConfig.layout.headingFoldGutterWidth
    readonly property int headingFoldIconSize: uiConfig.layout.headingFoldIconSize
    readonly property string headingFoldExpandedIconName: "chevron-down"
    readonly property string headingFoldCollapsedIconName: "chevron-right"
    readonly property color headingFoldExpandedColor: themeMutedTextColor
    readonly property color headingFoldCollapsedColor: themeAccentColor
    readonly property real headingNavigationHighlightOpacity:
        uiConfig.animation.headingNavigationHighlightOpacity
    readonly property int headingNavigationHighlightHoldDurationMs:
        uiConfig.animation.headingNavigationHighlightHoldDuration
    readonly property int headingNavigationHighlightFadeDurationMs:
        controller.animationsEnabled
            ? uiConfig.animation.headingNavigationHighlightFadeDuration : 0
    property var headingNavigationHighlightRectangles: []
    property real headingNavigationHighlightAnimationOpacity: 0
    readonly property bool headingNavigationHighlightVisible:
        headingNavigationHighlightRectangles.length > 0
            && headingNavigationHighlightAnimationOpacity > 0
    readonly property real headingNavigationHighlightEffectiveOpacity:
        headingNavigationHighlightAnimationOpacity * headingNavigationHighlightOpacity
    readonly property int headingNavigationHighlightRectCount:
        headingNavigationHighlightRectangles.length
    readonly property bool headingNavigationHighlightDrawnBeforeText:
        headingNavigationHighlightLayer.z < editor.z
    readonly property real headingNavigationHighlightMaxWidth: {
        let maximum = 0
        for (let index = 0; index < headingNavigationHighlightRectangles.length; ++index) {
            maximum = Math.max(maximum,
                               headingNavigationHighlightRectangles[index].width)
        }
        return maximum
    }
    readonly property int headingFoldMarkerCount: controller.headingFoldMarkers.length
    readonly property bool headingFoldActive: {
        for (let index = 0; index < controller.headingFoldMarkers.length; ++index) {
            if (controller.headingFoldMarkers[index].collapsed) {
                return true
            }
        }
        return false
    }
    readonly property rect headingFoldVisibleEndRectangle:
        editor.positionToRectangle(controller.headingFoldVisibleEndPosition)
    readonly property real editorVisibleContentHeight:
        headingFoldActive
            ? headingFoldVisibleEndRectangle.y + headingFoldVisibleEndRectangle.height
              + editor.contentHeight * 0
            : editor.contentHeight
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
    // 历史面板开/合的布局进度：几何直接绑定进度，缩放时即时跟随，
    // 只有开/合切换时由 Behavior 动画，避免窗口缩放期间逐帧重启动画。
    property real historyLayoutProgress: historyPanelOpen ? 1 : 0

    Behavior on historyLayoutProgress {
        NumberAnimation {
            duration: root.transitionDuration
            easing.type: Easing.OutCubic
        }
    }

    SequentialAnimation {
        id: headingNavigationHighlightAnimation

        PauseAnimation {
            duration: root.headingNavigationHighlightHoldDurationMs
        }
        NumberAnimation {
            target: root
            property: "headingNavigationHighlightAnimationOpacity"
            to: 0
            duration: root.headingNavigationHighlightFadeDurationMs
            easing.type: Easing.OutCubic
        }
        onFinished: root.headingNavigationHighlightRectangles = []
    }

    Connections {
        target: controller

        function onHeadingNavigationHighlightChanged() {
            root.restartHeadingNavigationHighlight()
        }
    }
    readonly property real historyPanelWidth:
        Math.max(uiConfig.panels.history.minWidth,
                 Math.min(uiConfig.panels.history.maxWidth, root.width / 3))
    readonly property bool historyPanelOverlay:
        (root.width - root.marginSize * 2 - historyPanelWidth)
        < uiConfig.panels.history.overlayThreshold
    readonly property real editorHorizontalShift:
        historyPanelOverlay ? 0 : historyLayoutProgress * historyPanelWidth
    readonly property real editorVisibleWidth:
        root.width - root.marginSize * 2 - editorHorizontalShift
    readonly property real editorViewportWidth: editorViewport.width
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
    property string historyHoveredId: ""
    // 内容高度防抖快照：仅驱动滚动条滑块尺寸（60ms 合并更新）；滑块可见性
    // 直接依赖实时 editorViewport.contentHeight，避免缩放/关闭动画期间快照
    // 滞后导致滚动条闪现。
    property real scrollContentHeight: 0
    property bool statusTextHovered: false
    property bool statusPanelHovered: false
    property bool statusPanelOpen: false
    property bool statusCopyFeedback: false
    property bool inputScrollHoldBottom: false
    // 标题跳转（Ctrl+Up/Down）期间由 C++ 置位：抑制跳转瞬间的光标瞬时贴边
    // 跟随，让整段跳转由统一的轻量滚动动画完成（跳转后标题锚定到视口上 1/3）。
    property bool suppressHeadingCursorFollow: false

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

    // 滑块尺寸的防抖快照更新；可见性不使用本快照（见 scrollThumb.visible）。
    function refreshScrollMetrics() {
        scrollContentHeight = editorViewport.contentHeight
    }

    function showFindPanel(withReplace) {
        commandPaletteLoader.active = false
        settingsLoader.active = false
        findPanel.open(withReplace)
    }

    function hideFindPanel() {
        findPanel.close()
        editor.forceActiveFocus()
    }

    function findInDocument(backwards) {
        findPanel.findInDocument(backwards)
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
        historyHoveredId = ""
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
        } else if (action === "historyItemHoverEnter") {
            historyPanelLoader.item.setHoveredId(value)
        } else if (action === "historyItemHoverLeave") {
            historyPanelLoader.item.clearHoveredId(value)
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
            id: headerTitle
            objectName: "headerTitle"
            x: root.headerTitleLeft - header.x
            y: root.headerTitleCenterY - header.y - height / 2
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

    FindPanel {
        id: findPanel
        appController: controller
        uiConfig: root.uiConfig
        dragZoneHeight: root.dragZoneHeight
        transitionDuration: root.transitionDuration
        panelColor: root.themePanelColor
        borderColor: root.themeBorderColor
        fieldColor: root.themeFieldColor
        focusColor: root.themeFocusColor
        textColor: root.themeTextColor
        mutedTextColor: root.themeMutedTextColor
        strongTextColor: root.themeStrongTextColor
        selectionColor: root.themeSelectionColor
        selectedTextColor: root.themeSelectedTextColor
        accentColor: root.themeAccentColor
        buttonColor: root.themeButtonColor
        buttonAccentTextColor: root.themeButtonAccentTextColor
        uiFontFamily: root.uiFontFamily
        monospaceFontFamily: root.uiMonospaceFontFamily
        onCloseRequested: root.hideFindPanel()
    }

    Rectangle {
        id: editorSurface
        x: root.marginSize + root.editorHorizontalShift
        y: root.editorContentTop
        width: root.editorVisibleWidth
        height: root.height - root.dragZoneHeight - root.marginSize
        color: root.themeEditorSurfaceColor

        Behavior on color {
            ColorAnimation { duration: root.transitionDuration }
        }
    }

    Flickable {
        id: editorViewport
        objectName: "editorViewport"
        x: root.marginSize + root.editorHorizontalShift
        y: root.editorContentTop
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
            root.editorVisibleContentHeight + (root.inputScrollHoldBottom
                ? Math.max(Math.max(height * 2 / 3, uiConfig.layout.editorContentBottomGap),
                           editorViewport.contentY + height - root.editorVisibleContentHeight)
                : Math.max(height * 2 / 3, uiConfig.layout.editorContentBottomGap)))
        pixelAligned: true

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

        Item {
            id: headingFoldGutter
            z: 3
            x: uiConfig.layout.editorPaddingX
            y: uiConfig.layout.editorPaddingY
            width: root.headingFoldGutterWidth
            height: editor.height

            Repeater {
                id: headingFoldRepeater
                model: controller.headingFoldMarkers

                delegate: Item {
                    required property var modelData
                    function activate() {
                        controller.toggleHeadingFoldAt(modelData.position)
                    }
                    readonly property string iconName: modelData.collapsed
                                                        ? root.headingFoldCollapsedIconName
                                                        : root.headingFoldExpandedIconName
                    readonly property bool iconValid: foldIcon.valid
                    readonly property int iconSize: foldIcon.size
                    readonly property real editorLayoutRevision:
                        editor.width + editor.contentHeight
                    readonly property rect headingRectangle:
                        editor.positionToRectangle(
                            modelData.position + editorLayoutRevision * 0)
                    x: 0
                    // 宽度与内容高度共同参与 headingRectangle 绑定，确保历史栏开合、
                    // 窗口缩放、折叠、换行和字号变化后重新计算标题第一视觉行。
                    y: Math.round(headingRectangle.y)
                    width: headingFoldGutter.width
                    height: Math.max(1, headingRectangle.height)

                    LucideIcon {
                        id: foldIcon
                        anchors.centerIn: parent
                        name: parent.iconName
                        size: root.headingFoldIconSize
                        color: modelData.collapsed
                               ? root.headingFoldCollapsedColor
                               : root.headingFoldExpandedColor
                    }

                    MouseArea {
                        objectName: "headingFoldMarker-" + modelData.position
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: parent.activate()
                    }
                }
            }
        }

        Item {
            id: headingNavigationHighlightLayer
            x: editor.x
            y: editor.y
            z: 0
            width: editor.width
            height: editor.height
            opacity: root.headingNavigationHighlightAnimationOpacity
            visible: root.headingNavigationHighlightVisible

            Repeater {
                model: root.headingNavigationHighlightRectangles

                delegate: Rectangle {
                    required property var modelData
                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    radius: uiConfig.layout.radiusSmall
                    color: root.themeAccentColor
                    opacity: root.headingNavigationHighlightOpacity
                }
            }
        }

        TextEdit {
            id: editor
            objectName: "scratchText"
            z: 1
            property int selectionDragPosition: -1
            readonly property rect selectionDragRectangle:
                selectionDragPosition >= 0
                    ? positionToRectangle(selectionDragPosition)
                    : Qt.rect(0, 0, 0, 0)
            x: uiConfig.layout.editorPaddingX + root.headingFoldGutterWidth
            y: uiConfig.layout.editorPaddingY
            width: editorViewport.width - uiConfig.layout.editorPaddingX * 2
                   - root.headingFoldGutterWidth
            height: Math.max(
                editorViewport.height - uiConfig.layout.editorPaddingY * 2, contentHeight)
            color: root.markdownTextColor
            selectionColor: root.themeSelectionColor
            selectedTextColor: root.themeSelectedTextColor
            font: controller.editorFont
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: true
            inputMethodHints: Qt.ImhMultiLine

            onWidthChanged: root.refreshHeadingNavigationHighlightGeometry()
            onContentHeightChanged: root.refreshHeadingNavigationHighlightGeometry()

            onCursorRectangleChanged: {
                // 标题跳转期间抑制瞬时贴边跟随，跳转的对齐滚动稍后由统一的
                // 轻量动画入口执行。
                if (root.suppressHeadingCursorFollow) {
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
                width: uiConfig.layout.selectionCursorWidth
                height: Math.max(1, editor.selectionDragRectangle.height)
                visible: editor.selectionDragPosition >= 0
                color: root.selectionDragColor
            }
        }
    }

    ClipboardHistoryPanel {
        id: historyPanelLoader
        anchors.fill: parent
        host: root
        appController: controller
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
        // 可见性用实时 contentHeight：窗口缩放/轻量关闭动画期间视口高度逐帧变化，
        // 60ms 防抖快照会持续滞后，短文本时滚动条会短暂误显示；滑块尺寸仍走防抖快照。
        visible: editorViewport.contentHeight > editorViewport.height + 0.5
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

    SettingsPage {
        id: settingsLoader
        anchors.fill: parent
        host: root
        appController: controller
    }
    CommandPalette {
        id: commandPaletteLoader
        anchors.fill: parent
        host: root
        appController: controller
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
