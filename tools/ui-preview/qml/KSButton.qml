import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: control
    property bool primary: false
    implicitWidth: 152
    implicitHeight: 40
    padding: Theme.s16
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    font.weight: Font.Medium
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    contentItem: KSText {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.white : Theme.muted
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.r8
        color: !control.enabled ? Theme.field : control.primary
               ? (control.down || control.hovered ? Theme.accentHover : Theme.accent)
               : (control.hovered ? Theme.border : Theme.surface)
        border.color: control.visualFocus ? Theme.white : control.primary ? "transparent" : Theme.border
        border.width: control.visualFocus ? 2 : 1
    }
}
