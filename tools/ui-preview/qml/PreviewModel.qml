import QtQuick 2.15

// Deliberately in-memory: no QSettings, IPC, subprocess, network or OS hooks.
QtObject {
    property bool paused: false
    property string transitionKey: "오른쪽 Alt"
    property bool ctrlToCommand: true
    property bool inputCheckActive: false
    property bool mappingExpanded: false
    readonly property string targetName: "MacBook Pro"
    readonly property string statusLabel: paused ? "일시정지" : "연결됨"
    function reset() {
        paused = false
        transitionKey = "오른쪽 Alt"
        ctrlToCommand = true
        inputCheckActive = false
        mappingExpanded = false
    }
}
