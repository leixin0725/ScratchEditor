import QtQuick
import QtQuick.Shapes

Item {
    id: root

    // Path data: https://github.com/lucide-icons/lucide (ISC/MIT, see third_party/lucide/LICENSE).
    required property string name
    property real size: 24
    property color color: "black"
    // Lucide 图标以 24 × 24 画布和 2 单位描边为基准。
    property real strokeWidth: 2

    readonly property bool valid: name === "chevron-down" || name === "chevron-right"
    readonly property var iconPoints: {
        switch (name) {
        case "chevron-down":
            return [Qt.point(6, 9), Qt.point(12, 15), Qt.point(18, 9)]
        case "chevron-right":
            return [Qt.point(9, 18), Qt.point(15, 12), Qt.point(9, 6)]
        default:
            return []
        }
    }

    width: size
    height: size

    Shape {
        anchors.fill: parent
        visible: root.valid

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth * Math.min(root.width, root.height) / 24
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            pathHints: ShapePath.PathLinear
            scale: Qt.size(root.width / 24, root.height / 24)

            PathPolyline {
                path: root.iconPoints
            }
        }
    }
}
