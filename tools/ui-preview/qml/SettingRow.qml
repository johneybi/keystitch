import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: row
    property string label
    property string value
    property int inset: 16
    implicitHeight: 56
    radius: Theme.r8
    color: Theme.surface
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: row.inset
        anchors.rightMargin: row.inset
        spacing: 12
        KSText { text: row.label; Layout.fillWidth: true }
        KSText { text: row.value; color: Theme.muted }
    }
}
