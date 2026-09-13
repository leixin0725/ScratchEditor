import QtQuick

Rectangle {
    id: root

    required property var appController
    required property var uiConfig
    required property int dragZoneHeight
    required property int transitionDuration
    required property real availableX
    required property real availableWidth
    required property color panelColor
    required property color borderColor
    required property color fieldColor
    required property color focusColor
    required property color textColor
    required property color mutedTextColor
    required property color strongTextColor
    required property color selectionColor
    required property color selectedTextColor
    required property color accentColor
    required property color buttonColor
    required property color buttonHoverTintColor
    required property color buttonAccentTextColor
    required property string uiFontFamily
    required property string monospaceFontFamily

    property bool replaceMode: false
    property string searchStatus: ""
    property string testHoveredButton: ""
    readonly property string queryText: findInput.text
    readonly property color previousButtonColor: findPrevButton.color
    readonly property color nextButtonColor: findNextButton.color
    readonly property color replaceAllButtonColor: replaceAllButton.color
    readonly property color caseSensitiveButtonColor: caseSensitiveToggle.color

    signal closeRequested()
    signal searchChanged()

    function buttonVisualColor(baseColor, buttonName, hovered, enabled) {
        return enabled && (hovered || testHoveredButton === buttonName)
                ? Qt.tint(baseColor, buttonHoverTintColor) : baseColor
    }

    function open(withReplace, initialQuery) {
        replaceMode = withReplace
        if (initialQuery !== undefined && initialQuery !== null)
            findInput.text = initialQuery
        visible = true
        searchStatus = ""
        Qt.callLater(function() {
            findInput.forceActiveFocus()
            findInput.selectAll()
        })
    }

    function close() {
        visible = false
        testHoveredButton = ""
    }

    function toggleCaseSensitiveForTest() {
        caseSensitiveToggle.enabledValue = !caseSensitiveToggle.enabledValue
    }

    function findInDocument(backwards) {
        if (findInput.text.length === 0) {
            searchStatus = "请输入查找内容"
            return
        }
        searchStatus = root.appController.findNext(findInput.text, caseSensitiveToggle.enabledValue,
                                           backwards) ? "已定位" : "未找到"
    }

    z: 60
    visible: false
    x: availableX + (availableWidth - width) / 2
    y: dragZoneHeight + uiConfig.panels.find.gap
    width: Math.min(uiConfig.panels.find.maxWidth,
                    availableWidth - uiConfig.panels.find.widthInset)
    height: replaceMode ? uiConfig.panels.find.heightReplace
                        : uiConfig.panels.find.heightSingle
    radius: uiConfig.layout.radiusNormal
    color: panelColor
    border.color: borderColor
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
        width: root.width - uiConfig.panels.find.controlsWidth
        height: uiConfig.layout.controlHeightSmall
        radius: uiConfig.layout.radiusSmall
        color: root.fieldColor
        border.color: findInput.activeFocus ? root.focusColor : root.borderColor

        TextInput {
            id: findInput
            onTextChanged: root.searchChanged()
            anchors.fill: parent
            anchors.leftMargin: uiConfig.layout.spacingInput
            anchors.rightMargin: uiConfig.layout.spacingInput
            verticalAlignment: TextInput.AlignVCenter
            color: root.textColor
            selectionColor: root.selectionColor
            selectedTextColor: root.selectedTextColor
            font.family: root.uiFontFamily
            font.pointSize: uiConfig.fonts.normal
            selectByMouse: true
            clip: true

            Keys.onReturnPressed: function(event) {
                root.findInDocument((event.modifiers & Qt.ShiftModifier) !== 0)
            }
            Keys.onEnterPressed: function(event) {
                root.findInDocument((event.modifiers & Qt.ShiftModifier) !== 0)
            }
            Keys.onEscapePressed: root.closeRequested()
        }
    }

    Rectangle {
        id: caseSensitiveToggle
        property bool enabledValue: false
        onEnabledValueChanged: root.searchChanged()
        x: findFieldFrame.x + findFieldFrame.width + uiConfig.panels.find.gap
        y: uiConfig.panels.find.paddingY
        width: uiConfig.panels.find.caseSensitiveWidth
        height: uiConfig.layout.controlHeightSmall
        radius: uiConfig.layout.radiusSmall
        color: root.buttonVisualColor(enabledValue ? root.accentColor : root.buttonColor,
                                      "caseSensitive", caseSensitivePointer.containsMouse, true)

        Text {
            anchors.centerIn: parent
            text: "Aa"
            color: root.strongTextColor
            font.family: root.monospaceFontFamily
            font.pointSize: uiConfig.fonts.small
        }

        MouseArea {
            id: caseSensitivePointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: caseSensitiveToggle.enabledValue = !caseSensitiveToggle.enabledValue
        }
    }

    Rectangle {
        id: findPrevButton
        x: caseSensitiveToggle.x + caseSensitiveToggle.width + uiConfig.panels.find.gap
        y: uiConfig.panels.find.paddingY
        width: uiConfig.panels.find.prevWidth
        height: uiConfig.layout.controlHeightSmall
        radius: uiConfig.layout.radiusSmall
        color: root.buttonVisualColor(root.buttonColor, "previous",
                                      findPrevPointer.containsMouse, true)
        Text {
            anchors.centerIn: parent
            text: "上一个"
            color: root.textColor
            font.pointSize: uiConfig.fonts.small
        }
        MouseArea {
            id: findPrevPointer
            anchors.fill: parent
            hoverEnabled: true
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
        color: root.buttonVisualColor(root.buttonColor, "next",
                                      findNextPointer.containsMouse, true)
        Text {
            anchors.centerIn: parent
            text: "下一个"
            color: root.textColor
            font.pointSize: uiConfig.fonts.small
        }
        MouseArea {
            id: findNextPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.findInDocument(false)
        }
    }

    Rectangle {
        id: closeButton
        x: root.width - uiConfig.panels.find.rightInset
        y: uiConfig.panels.find.paddingY
        width: uiConfig.panels.find.closeWidth
        height: uiConfig.layout.controlHeightSmall
        radius: uiConfig.layout.radiusSmall
        color: root.buttonVisualColor(root.buttonColor, "close",
                                      closePointer.containsMouse, true)
        Text {
            anchors.centerIn: parent
            text: "×"
            color: root.textColor
            font.pointSize: uiConfig.fonts.title
        }
        MouseArea {
            id: closePointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.closeRequested()
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
        color: root.fieldColor
        border.color: replaceInput.activeFocus ? root.focusColor : root.borderColor

        TextInput {
            id: replaceInput
            anchors.fill: parent
            anchors.leftMargin: uiConfig.layout.spacingInput
            anchors.rightMargin: uiConfig.layout.spacingInput
            verticalAlignment: TextInput.AlignVCenter
            color: root.textColor
            selectionColor: root.selectionColor
            selectedTextColor: root.selectedTextColor
            font.family: root.uiFontFamily
            font.pointSize: uiConfig.fonts.normal
            selectByMouse: true
            clip: true
            Keys.onEscapePressed: root.closeRequested()
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
        color: root.buttonVisualColor(root.buttonColor, "replaceCurrent",
                                      replaceCurrentPointer.containsMouse, true)
        Text {
            anchors.centerIn: parent
            text: "替换当前"
            color: root.textColor
            font.pointSize: uiConfig.fonts.small
        }
        MouseArea {
            id: replaceCurrentPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.searchStatus = root.appController.replaceCurrent(
                    findInput.text, replaceInput.text, caseSensitiveToggle.enabledValue
                ) ? "已处理当前匹配" : "未找到"
            }
        }
    }

    Rectangle {
        id: replaceAllButton
        visible: root.replaceMode
        x: replaceCurrentButton.x + replaceCurrentButton.width + uiConfig.panels.find.gap
        y: replaceFieldFrame.y
        width: uiConfig.panels.find.actionWidth
        height: uiConfig.layout.controlHeightSmall
        radius: uiConfig.layout.radiusSmall
        color: root.buttonVisualColor(root.buttonColor, "replaceAll",
                                      replaceAllPointer.containsMouse, true)
        Text {
            anchors.centerIn: parent
            text: "全部替换"
            color: root.textColor
            font.pointSize: uiConfig.fonts.small
        }
        MouseArea {
            id: replaceAllPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const count = root.appController.replaceAll(
                    findInput.text, replaceInput.text, caseSensitiveToggle.enabledValue
                )
                root.searchStatus = "已替换 " + count + " 处"
            }
        }
    }

    Text {
        x: root.width - uiConfig.panels.find.statusWidth - uiConfig.panels.find.paddingX
        y: root.replaceMode ? uiConfig.panels.find.statusYReplace
                            : uiConfig.panels.find.statusY
        width: uiConfig.panels.find.statusWidth
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        text: root.searchStatus
        color: root.mutedTextColor
        font.family: root.uiFontFamily
        font.pointSize: uiConfig.fonts.caption
    }
}
