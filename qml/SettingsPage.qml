import QtQuick

Item {
    id: root
    z: 90

    required property var host
    required property var appController
    readonly property var uiConfig: host.uiConfig
    property alias active: settingsLoader.active
    readonly property var item: settingsLoader.item

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
            property string draftFallbackFontFamily: ""
            property int draftFontPointSize: uiConfig.fonts.editorDefaultSize
            property int draftFontWeight: uiConfig.fonts.editorDefaultWeight
            property bool fontWeightMenuOpen: false
            property var fontWeightOptions: [
                { "label": "细体", "value": 100 },
                { "label": "特细", "value": 200 },
                { "label": "轻体", "value": 300 },
                { "label": "常规", "value": 400 },
                { "label": "中等", "value": 500 },
                { "label": "半粗", "value": 600 },
                { "label": "粗体", "value": 700 },
                { "label": "特粗", "value": 800 },
                { "label": "黑体", "value": 900 }
            ]
            property bool draftAnimationsEnabled: uiConfig.preferences.animationsEnabled
            property int draftStatusPanelFontSize: uiConfig.panels.statusPanel.defaultFontSize
            property int draftStatusPanelShowDelayMs:
                uiConfig.panels.statusPanel.defaultShowDelayMs
            property int draftStatusPanelHideDelayMs:
                uiConfig.panels.statusPanel.defaultHideDelayMs
            property int draftStatusPanelMaxWidth:
                uiConfig.panels.statusPanel.defaultMaxWidth
            property string saveStatus: ""

            function fontWeightLabel(weight) {
                for (let index = 0; index < fontWeightOptions.length; ++index) {
                    const option = fontWeightOptions[index]
                    if (option.value === weight)
                        return option.label + "（" + option.value + "）"
                }
                return weight.toString()
            }

            Behavior on opacity {
                NumberAnimation { duration: host.transitionDuration; easing.type: Easing.OutCubic }
            }

            function activate() {
                draftTheme = root.appController.theme
                draftFontFamily = root.appController.editorFontFamily
                draftFallbackFontFamily = root.appController.editorFallbackFontFamily
                draftFontPointSize = root.appController.editorFontPointSize
                draftFontWeight = root.appController.editorFontWeight
                fontWeightMenuOpen = false
                draftAnimationsEnabled = root.appController.animationsEnabled
                draftStatusPanelFontSize = root.appController.statusPanelFontSize
                draftStatusPanelShowDelayMs = root.appController.statusPanelShowDelayMs
                draftStatusPanelHideDelayMs = root.appController.statusPanelHideDelayMs
                draftStatusPanelMaxWidth = root.appController.statusPanelMaxWidth
                fontFamilyInput.text = draftFontFamily
                fallbackFontFamilyInput.text = draftFallbackFontFamily
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
                if (root.appController.applyAppearance(draftTheme, fontFamilyInput.text,
                                               fallbackFontFamilyInput.text, requestedSize,
                                               draftFontWeight,
                                               draftAnimationsEnabled)
                    && root.appController.applyStatusPanelSettings(panelFontSize, panelShowDelayMs,
                                                           panelHideDelayMs, panelMaxWidth)) {
                    saveStatus = "设置已保存"
                    draftFontFamily = root.appController.editorFontFamily
                    draftFallbackFontFamily = root.appController.editorFallbackFontFamily
                    draftFontPointSize = root.appController.editorFontPointSize
                    draftFontWeight = root.appController.editorFontWeight
                    draftStatusPanelFontSize = root.appController.statusPanelFontSize
                    draftStatusPanelShowDelayMs = root.appController.statusPanelShowDelayMs
                    draftStatusPanelHideDelayMs = root.appController.statusPanelHideDelayMs
                    draftStatusPanelMaxWidth = root.appController.statusPanelMaxWidth
                } else {
                    saveStatus = root.appController.settingsError
                }
            }

            Keys.onEscapePressed: function(event) {
                if (fontWeightMenuOpen) {
                    fontWeightMenuOpen = false
                    event.accepted = true
                    return
                }
                host.closeSettings()
            }

            Rectangle {
                anchors.fill: parent
                color: host.overlayColor
                MouseArea {
                    anchors.fill: parent
                    onClicked: host.closeSettings()
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
                color: host.themePanelColor
                border.color: host.themeBorderColor
                border.width: uiConfig.layout.borderWidth

                Behavior on color {
                    ColorAnimation { duration: host.transitionDuration }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) {
                        settingsRoot.fontWeightMenuOpen = false
                        mouse.accepted = true
                    }
                }

                Text {
                    x: uiConfig.panels.settingsPage.paddingX
                    y: uiConfig.panels.settingsPage.titleY
                    text: "设置"
                    color: host.themeStrongTextColor
                    font.family: host.uiFontFamily
                    font.pointSize: uiConfig.fonts.title
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: uiConfig.panels.settingsPage.closeMarginX
                    y: uiConfig.panels.settingsPage.titleY
                    text: "×"
                    color: host.themeMutedTextColor
                    font.pointSize: uiConfig.fonts.title
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: uiConfig.panels.settingsPage.closeHitInset
                        cursorShape: Qt.PointingHandCursor
                        onClicked: host.closeSettings()
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
                    onMovementStarted: settingsRoot.fontWeightMenuOpen = false

                    Item {
                        width: settingsContent.width
                        height: settingsContent.contentHeight

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 0
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "主题"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 0
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: settingsRoot.draftTheme === "dark"
                                   ? host.panelAccentColor : host.themeButtonColor
                            border.color: host.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: "深色"
                                color: settingsRoot.draftTheme === "dark"
                                       ? host.panelAccentTextColor : host.themeTextColor
                                font.family: host.uiFontFamily
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
                                   ? host.panelAccentColor : host.themeButtonColor
                            border.color: host.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: "浅色"
                                color: settingsRoot.draftTheme === "light"
                                       ? host.panelAccentTextColor : host.themeTextColor
                                font.family: host.uiFontFamily
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
                            text: "ASCII 字体"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 1
                            width: parent.width - uiConfig.panels.settingsPage.columnX
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: fontFamilyInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: fontFamilyInput
                                anchors.fill: parent
                                anchors.leftMargin: uiConfig.layout.spacingLarge
                                anchors.rightMargin: uiConfig.layout.spacingLarge
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
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
                            text: "中文 fallback 字体"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 2
                            width: parent.width - uiConfig.panels.settingsPage.columnX
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: fallbackFontFamilyInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: fallbackFontFamilyInput
                                anchors.fill: parent
                                anchors.leftMargin: uiConfig.layout.spacingLarge
                                anchors.rightMargin: uiConfig.layout.spacingLarge
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                selectByMouse: true
                                clip: true
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 3
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "字号（" + uiConfig.fonts.editorSizeMin
                                  + "–" + uiConfig.fonts.editorSizeMax + "）"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 3
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: fontSizeInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: fontSizeInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.fonts.editorSizeMin
                                    top: uiConfig.fonts.editorSizeMax
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 4
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "字体粗细"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            id: fontWeightSelector
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 4
                            width: uiConfig.panels.settingsPage.labelWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: settingsRoot.fontWeightMenuOpen
                                          ? host.panelAccentColor : host.themeBorderColor
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: uiConfig.layout.spacingLarge
                                anchors.verticalCenter: parent.verticalCenter
                                text: settingsRoot.fontWeightLabel(
                                          settingsRoot.draftFontWeight)
                                color: host.themeTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: uiConfig.layout.spacingMedium
                                anchors.verticalCenter: parent.verticalCenter
                                text: "▾"
                                color: host.themeMutedTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.small
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: settingsRoot.fontWeightMenuOpen =
                                           !settingsRoot.fontWeightMenuOpen
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 5
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "轻量动画"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 5
                            width: uiConfig.panels.settingsPage.controlWidth
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusPill
                            color: settingsRoot.draftAnimationsEnabled
                                   ? host.panelAccentColor : host.themeButtonColor
                            border.color: host.themeBorderColor
                            Text {
                                anchors.centerIn: parent
                                text: settingsRoot.draftAnimationsEnabled ? "开启" : "关闭"
                                color: settingsRoot.draftAnimationsEnabled
                                       ? host.panelAccentTextColor : host.themeTextColor
                                font.family: host.uiFontFamily
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
                            y: uiConfig.panels.settingsPage.rowHeight * 6
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "面板字号（" + uiConfig.panels.statusPanel.fontSizeMin
                                  + "–" + uiConfig.panels.statusPanel.fontSizeMax + "）"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 6
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: statusPanelFontSizeInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: statusPanelFontSizeInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.fontSizeMin
                                    top: uiConfig.panels.statusPanel.fontSizeMax
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 7
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "显示延迟（毫秒）"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 7
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: statusPanelShowDelayInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: statusPanelShowDelayInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.showDelayMinMs
                                    top: uiConfig.panels.statusPanel.showDelayMaxMs
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 8
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "收起延迟（毫秒）"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 8
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: statusPanelHideDelayInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: statusPanelHideDelayInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.normal
                                validator: IntValidator {
                                    bottom: uiConfig.panels.statusPanel.hideDelayMinMs
                                    top: uiConfig.panels.statusPanel.hideDelayMaxMs
                                }
                            }
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.rowHeight * 9
                               + uiConfig.panels.settingsPage.labelYOffset
                            width: uiConfig.panels.settingsPage.labelWidth
                            text: "最大宽度（像素）"
                            color: host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Rectangle {
                            x: uiConfig.panels.settingsPage.columnX
                            y: uiConfig.panels.settingsPage.rowHeight * 9
                            width: uiConfig.panels.settingsPage.controlWidthWide
                            height: uiConfig.layout.controlHeightNormal
                            radius: uiConfig.layout.radiusNormal
                            color: host.themeFieldColor
                            border.color: statusPanelMaxWidthInput.activeFocus
                                          ? host.panelAccentColor : host.themeBorderColor
                            TextInput {
                                id: statusPanelMaxWidthInput
                                anchors.fill: parent
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                color: host.themeTextColor
                                selectionColor: host.panelAccentColor
                                selectedTextColor: host.panelAccentTextColor
                                font.family: host.uiFontFamily
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
                            color: host.themeMutedTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.caption
                        }

                        Text {
                            x: 0
                            y: uiConfig.panels.settingsPage.contentHeight
                               - uiConfig.panels.settingsPage.captionBottomGap
                            width: parent.width
                            text: root.appController.settingsFile
                            color: host.themeMutedTextColor
                            font.family: host.uiMonospaceFontFamily
                            font.pointSize: uiConfig.fonts.caption
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Rectangle {
                    id: fontWeightMenu
                    readonly property real optionHeight:
                        uiConfig.panels.settingsPage.rowHeight / 2
                    readonly property real selectorY: settingsContent.y
                        + uiConfig.panels.settingsPage.rowHeight * 4
                        - settingsContent.contentY
                    x: settingsContent.x + uiConfig.panels.settingsPage.columnX
                    y: selectorY + uiConfig.layout.controlHeightNormal + height
                       <= settingsPanel.height - uiConfig.layout.spacingSmall
                       ? selectorY + uiConfig.layout.controlHeightNormal
                       : Math.max(uiConfig.layout.spacingSmall, selectorY - height)
                    z: 20
                    width: uiConfig.panels.settingsPage.labelWidth
                    height: settingsRoot.fontWeightOptions.length * optionHeight
                            + border.width * 2
                    visible: settingsRoot.fontWeightMenuOpen
                    radius: uiConfig.layout.radiusNormal
                    color: host.themeFieldColor
                    border.color: host.themeBorderColor
                    border.width: uiConfig.layout.borderWidth
                    clip: true

                    ListView {
                        anchors.fill: parent
                        anchors.margins: fontWeightMenu.border.width
                        interactive: false
                        model: settingsRoot.fontWeightOptions
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: fontWeightMenu.optionHeight
                            color: modelData.value === settingsRoot.draftFontWeight
                                   ? host.panelAccentColor : host.themeFieldColor
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label + "（" + modelData.value + "）"
                                color: modelData.value === settingsRoot.draftFontWeight
                                       ? host.panelAccentTextColor : host.themeTextColor
                                font.family: host.uiFontFamily
                                font.pointSize: uiConfig.fonts.small
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    settingsRoot.draftFontWeight = modelData.value
                                    settingsRoot.fontWeightMenuOpen = false
                                }
                            }
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
                           ? host.themeMutedTextColor : host.themeDangerColor
                    font.family: host.uiFontFamily
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
                    color: host.themeButtonColor
                    border.color: host.themeBorderColor
                    Text {
                        anchors.centerIn: parent
                        text: "恢复默认"
                        color: host.themeTextColor
                        font.family: host.uiFontFamily
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.appController.resetAppearance()
                            root.appController.resetStatusPanelSettings()
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
                    color: host.panelAccentColor
                    Text {
                        anchors.centerIn: parent
                        text: "应用"
                        color: host.panelAccentTextColor
                        font.family: host.uiFontFamily
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

}
