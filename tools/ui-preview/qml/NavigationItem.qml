import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: control
    property url iconSource
    property bool selected: false
    implicitWidth: 188
    implicitHeight: 40
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    contentItem: RowLayout {
        spacing: 12
        Image { source: control.iconSource; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
        KSText { text: control.text; Layout.fillWidth: true }
    }
    leftPadding: 16
    rightPadding: 16
    background: Rectangle {
        radius: Theme.r8
        color: control.selected ? Theme.accent : control.hovered ? Theme.surface : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.white
    }
}
