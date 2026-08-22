import QtQuick

Loader {
    id: root

    required property var host
    required property var appController
    readonly property var uiConfig: host.uiConfig
    z: 35
    x: host.marginSize
    y: host.dragZoneHeight
    width: host.historyPanelWidth
    height: host.height - host.dragZoneHeight - host.marginSize
    clip: true
    active: root.appController.clipboardHistoryAvailable

    sourceComponent: Item {
        id: historyRoot
        readonly property bool queryFocused: historyQuery.activeFocus
        readonly property real edgeIntrusion:
            host.historyPanelOpen ? 0
                                  : Math.max(0, historyPanel.x + historyPanel.width)

        function focusQuery() {
            historyQuery.forceActiveFocus()
            historyQuery.selectAll()
        }
        function clearQuery() { historyQuery.text = "" }
        function setQuery(value) {
            historyQuery.text = value === undefined ? "" : value
            root.appController.setClipboardHistoryFilter(historyQuery.text)
        }
        function selectId(value) {
            host.historySelectedId = value
            root.appController.selectClipboardHistoryItem(value)
            for (let i = 0; i < historyList.count; ++i) {
                const row = historyList.itemAtIndex(i)
                if (row && row.itemId === value) {
                    historyList.currentIndex = i
                    return
                }
            }
        }
        function activateSelected() {
            if (host.historySelectedId.length > 0) {
                root.appController.requestLoadClipboardHistory(host.historySelectedId)
            }
        }
        function setHoveredId(value) {
            host.historyHoveredId = value === undefined ? "" : value
        }
        function clearHoveredId(value) {
            if (value === undefined || host.historyHoveredId === value) {
                host.historyHoveredId = ""
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
            objectName: "clipboardHistoryPanel"
            // 闭合时右边缘正好压在 loader 左边界（左边框与编辑区域交界）。
            // 滑动进度只在开/合切换时变化并带动画；x 直接由宽度与进度绑定，
            // 窗口缩放期间宽度变化会让闭合 x 即时跟随，避免 Behavior 逐帧
            // 重启动画造成右边缘短暂探入可见裁剪区（唤出窗口时闪现）。
            property real slideProgress: host.historyPanelOpen ? 1 : 0
            x: -historyPanel.width + historyPanel.width * slideProgress
            width: host.historyPanelWidth
            height: parent.height
            // 左侧使用历史面板专用内部圆角；右侧与编辑区拼接，保持直角。
            topLeftRadius: uiConfig.panels.history.cornerRadius
            topRightRadius: 0
            bottomLeftRadius: uiConfig.panels.history.cornerRadius
            bottomRightRadius: 0
            color: host.themePanelColor
            border.color: host.themeBorderColor
            border.width: uiConfig.layout.borderWidth

            Behavior on slideProgress {
                NumberAnimation {
                    duration: host.transitionDuration
                    easing.type: Easing.OutCubic
                }
            }

            HoverHandler {
                onHoveredChanged: {
                    host.historyPanelHovered = hovered
                    if (hovered) historyCloseTimer.stop()
                    else host.scheduleClipboardHistoryClose()
                }
            }

            Text {
                id: historyTitle
                objectName: "historyTitle"
                x: host.headerTitleLeft
                y: host.headerTitleCenterY - height / 2
                text: "剪贴板历史"
                color: host.themeStrongTextColor
                font.family: host.uiFontFamily
                font.pointSize: uiConfig.fonts.heading
                font.weight: Font.DemiBold
            }

            Rectangle {
                id: historySearchFrame
                objectName: "historySearchFrame"
                x: uiConfig.panels.history.searchX
                y: host.editorContentTop
                width: parent.width - uiConfig.panels.history.searchInsetX
                height: uiConfig.layout.controlHeightNormal
                radius: uiConfig.layout.radiusNormal
                color: host.themeFieldColor
                border.color: historyQuery.activeFocus
                              ? host.themeFocusColor : host.themeBorderColor

                TextInput {
                    id: historyQuery
                    anchors.fill: parent
                    anchors.margins: uiConfig.panels.history.inputMargin
                    color: host.themeTextColor
                    selectionColor: host.themeSelectionColor
                    selectedTextColor: host.themeSelectedTextColor
                    font.family: host.uiFontFamily
                    font.pointSize: uiConfig.fonts.small
                    clip: true
                    onTextChanged: root.appController.setClipboardHistoryFilter(text)
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
                    color: host.themeMutedTextColor
                    font.family: host.uiFontFamily
                    font.pointSize: uiConfig.fonts.small
                }
            }

            ListView {
                id: historyList
                objectName: "historyList"
                x: uiConfig.panels.history.listX
                y: historySearchFrame.y + historySearchFrame.height
                   + uiConfig.layout.spacingMedium
                width: parent.width - uiConfig.panels.history.listInsetX
                height: parent.height - y
                        - uiConfig.panels.history.footerBottomGap
                        - uiConfig.layout.controlHeightCompact
                        - uiConfig.layout.spacingLarge
                clip: true
                spacing: uiConfig.layout.spacingSmall
                model: root.appController.clipboardHistoryModel
                currentIndex: count > 0 ? Math.max(0, currentIndex) : -1
                onCountChanged: {
                    if (count > 0 && host.historySelectedId.length === 0) {
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
                    height: root.appController.historyCardHeight
                    radius: uiConfig.layout.radiusNormal
                    color: (host.historySelectedId === historyId
                            || host.historyHoveredId === historyId)
                           ? host.themeButtonColor : "transparent"
                    border.color: host.historySelectedId === historyId
                                  ? host.themeAccentColor : "transparent"

                    Text {
                        x: uiConfig.panels.history.cardTextX
                        y: uiConfig.panels.history.cardTextY
                        width: parent.width - uiConfig.panels.history.cardTextInsetX
                        height: parent.height - uiConfig.panels.history.cardMetaHeight
                        text: historyRow.previewText
                        color: host.themeTextColor
                        font.family: host.uiFontFamily
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
                        color: host.themeMutedTextColor
                        font.family: host.uiFontFamily
                        font.pointSize: uiConfig.fonts.caption
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        onEntered: historyRoot.setHoveredId(historyRow.historyId)
                        onExited: historyRoot.clearHoveredId(historyRow.historyId)
                        onClicked: historyRoot.selectId(historyRow.historyId)
                        onDoubleClicked: {
                            historyRoot.selectId(historyRow.historyId)
                            historyRoot.activateSelected()
                        }
                    }
                    Component.onDestruction: historyRoot.clearHoveredId(historyRow.historyId)
                }
            }

            Text {
                anchors.centerIn: historyList
                visible: historyList.count === 0
                text: root.appController.clipboardHistoryError.length > 0
                      ? root.appController.clipboardHistoryError : "暂无剪贴板历史"
                color: root.appController.clipboardHistoryError.length > 0
                       ? host.themeDangerColor : host.themeMutedTextColor
                width: historyList.width - uiConfig.panels.history.emptyTextInsetX
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.family: host.uiFontFamily
                font.pointSize: uiConfig.fonts.small
            }

            Rectangle {
                objectName: "historyDeleteButton"
                x: uiConfig.panels.history.footerMarginX
                anchors.bottom: parent.bottom
                anchors.bottomMargin: uiConfig.panels.history.footerBottomGap
                width: uiConfig.panels.history.footerButtonWidth
                height: uiConfig.layout.controlHeightCompact
                radius: uiConfig.layout.radiusNormal
                color: host.themeButtonColor
                Text {
                    anchors.centerIn: parent
                    text: "删除"
                    color: host.themeTextColor
                    font.pointSize: uiConfig.fonts.small
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: host.historySelectedId.length > 0
                    onClicked: root.appController.deleteClipboardHistoryItem(host.historySelectedId)
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
                color: host.themeButtonColor
                Text {
                    anchors.centerIn: parent
                    text: "清空"
                    color: host.themeDangerColor
                    font.pointSize: uiConfig.fonts.small
                }
                MouseArea { anchors.fill: parent; onClicked: root.appController.requestClearClipboardHistory() }
            }
        }

    }
}
