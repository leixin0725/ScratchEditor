import QtQuick

Item {
    id: root
    z: 80

    required property var host
    required property var appController
    readonly property var uiConfig: host.uiConfig
    property alias active: commandPaletteLoader.active
    readonly property var item: commandPaletteLoader.item

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
                NumberAnimation { duration: host.transitionDuration; easing.type: Easing.OutCubic }
            }

            function rebuild() {
                const needle = paletteQuery.text.trim().toLowerCase()
                const source = root.appController.commands
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
                host.closeCommandPalette()
                root.appController.executeCommand(command.id)
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
                if (root.appController.setShortcut(editingCommandId, shortcutEditor.text)) {
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
                color: host.overlayColor
                MouseArea {
                    anchors.fill: parent
                    onClicked: host.closeCommandPalette()
                }
            }

            Rectangle {
                id: palettePanel
                x: Math.round((parent.width - width) / 2)
                y: Math.max(uiConfig.panels.commandPalette.minTop,
                            Math.round(parent.height * uiConfig.panels.commandPalette.topRatio))
                width: Math.min(host.commandPaletteMaximumWidth,
                                parent.width - uiConfig.panels.commandPalette.widthInset)
                height: Math.min(uiConfig.panels.commandPalette.maxHeight,
                                 parent.height - y - uiConfig.panels.commandPalette.bottomGap)
                radius: uiConfig.layout.radiusLarge
                color: host.themePanelColor
                border.color: host.themeBorderColor
                border.width: uiConfig.layout.borderWidth

                Behavior on color {
                    ColorAnimation { duration: host.transitionDuration }
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
                    color: host.themeFieldColor
                    border.color: paletteQuery.activeFocus
                                  ? host.panelAccentColor : host.themeBorderColor

                    TextInput {
                        id: paletteQuery
                        anchors.fill: parent
                        anchors.leftMargin: uiConfig.panels.commandPalette.textMarginX
                        anchors.rightMargin: uiConfig.panels.commandPalette.textMarginX
                        verticalAlignment: TextInput.AlignVCenter
                        color: host.themeTextColor
                        selectionColor: host.panelAccentColor
                        selectedTextColor: host.panelAccentTextColor
                        font.family: host.uiFontFamily
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
                                host.closeCommandPalette()
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
                               ? host.panelAccentColor : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: uiConfig.panels.commandPalette.rowTextMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: index === paletteRoot.selectedIndex
                                   ? host.panelAccentTextColor : host.themeTextColor
                            font.family: host.uiFontFamily
                            font.pointSize: uiConfig.fonts.normal
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: uiConfig.panels.commandPalette.rowTextMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.shortcut
                            color: index === paletteRoot.selectedIndex
                                   ? host.panelAccentTextColor : host.themeMutedTextColor
                            font.family: host.uiMonospaceFontFamily
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
                    color: host.themeMutedTextColor
                    elide: Text.ElideRight
                    font.family: host.uiFontFamily
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
                    color: host.themeFieldColor
                    border.color: host.panelAccentColor

                    TextInput {
                        id: shortcutEditor
                        anchors.fill: parent
                        anchors.leftMargin: uiConfig.panels.commandPalette.rowTextMargin
                        anchors.rightMargin: uiConfig.panels.commandPalette.rowTextMargin
                        verticalAlignment: TextInput.AlignVCenter
                        color: host.themeTextColor
                        selectionColor: host.panelAccentColor
                        selectedTextColor: host.panelAccentTextColor
                        font.family: host.uiMonospaceFontFamily
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

}
