pragma ComponentBehavior: Bound
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: window
    objectName: "previewWindow"
    width: 1000
    height: 720
    minimumWidth: 880
    minimumHeight: 620
    visible: true
    title: "KeyStitch — 독립 UI 미리보기"
    color: Theme.canvas
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    palette.window: Theme.canvas
    palette.windowText: Theme.text
    palette.base: Theme.field
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.white
    property alias model: preview
    PreviewModel { id: preview; objectName: "previewModel" }

    Rectangle {
        id: sidebar
        width: 220
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.sidebar
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.s16
            spacing: Theme.s8
            KSText {
                text: "KeyStitch"; font.pixelSize: Theme.fontSection
                font.bold: true; Layout.preferredHeight: 26
            }
            TextField {
                id: search
                objectName: "settingsSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                placeholderText: "설정 검색"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: Theme.fontCaption
                leftPadding: 32
                selectByMouse: true
                Accessible.name: "설정 검색"
                background: Rectangle {
                    color: Theme.field; radius: Theme.r8
                    border.width: search.activeFocus ? 1 : 0; border.color: Theme.accent
                }
                Image {
                    source: "../assets/figma/imgIconSearch.svg"
                    width: 16; height: 16; x: 8; anchors.verticalCenter: parent.verticalCenter
                }
            }
            Item { Layout.preferredHeight: 12 }
            Repeater {
                model: [
                    {label: "기기와 화면", icon: "Monitor"},
                    {label: "키보드", icon: "Keyboard"},
                    {label: "마우스", icon: "Mouse"},
                    {label: "클립보드", icon: "Clipboard"}
                ]
                NavigationItem {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    iconSource: "../assets/figma/imgIcon" + modelData.icon + ".svg"
                    selected: text === "키보드"
                    visible: !search.text || text.indexOf(search.text) >= 0
                    onClicked: if (text === "키보드") search.text = ""; else info.open()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            Repeater {
                model: [{label: "일반", icon: "Settings"}, {label: "문제 해결", icon: "Wrench"}]
                NavigationItem {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    iconSource: "../assets/figma/imgIcon" + modelData.icon + ".svg"
                    visible: !search.text || text.indexOf(search.text) >= 0
                    onClicked: info.open()
                }
            }
            Item { Layout.fillHeight: true }
            KSText { text: "독립 UI 미리보기"; color: Theme.muted; font.pixelSize: Theme.fontCaption; Layout.preferredHeight: 20 }
        }
    }

    ScrollView {
        id: scroll
        objectName: "contentScroll"
        anchors.left: sidebar.right
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 64
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Column {
            id: content
            x: Theme.s32; y: Theme.s32
            width: scroll.availableWidth - 64
            spacing: Theme.s24
            RowLayout {
                width: parent.width; height: 40; spacing: 12
                KSText { text: "키보드"; font.pixelSize: Theme.fontTitle; font.bold: true; lineHeight: 38; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                RowLayout {
                    Layout.preferredWidth: 116; Layout.minimumWidth: 116; Layout.maximumWidth: 116; spacing: 12
                    Layout.preferredHeight: 32; Layout.alignment: Qt.AlignTop
                    Image { source: "../assets/figma/imgEllipse.svg"; Layout.preferredWidth: 8; Layout.preferredHeight: 8; visible: !preview.paused }
                    Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: Theme.warning; visible: preview.paused }
                    KSText { text: preview.statusLabel; font.pixelSize: Theme.fontCaption; Layout.fillWidth: true }
                }
                KSButton { objectName: "pauseButton"; text: preview.paused ? "재개" : "일시정지"; onClicked: preview.paused = !preview.paused }
            }
            RowLayout {
                width: parent.width; height: 40; spacing: 16
                KSText { text: "적용 대상"; color: Theme.muted; Layout.alignment: Qt.AlignTop }
                KSButton { objectName: "targetButton"; text: preview.targetName; onClicked: targetInfo.open() }
                KSText { text: "Windows PC에서 전달되는 키"; font.pixelSize: Theme.fontCaption; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.WordWrap; Layout.alignment: Qt.AlignTop }
            }
            Rectangle {
                width: parent.width; height: 208; radius: Theme.r8; color: Theme.surface
                Column {
                    anchors.fill: parent; anchors.margins: 24; spacing: 12
                    KSText { text: "한영 전환"; font.pixelSize: Theme.fontSection; font.bold: true; height: 26 }
                    RowLayout {
                        width: parent.width; height: 40; spacing: 12
                        KSText { text: "전환 키"; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                        Rectangle {
                            Layout.preferredWidth: 122; Layout.preferredHeight: 36
                            Layout.alignment: Qt.AlignTop
                            color: Theme.field; radius: Theme.r8; border.color: Theme.border
                            KSText { text: preview.transitionKey; anchors.centerIn: parent }
                        }
                        KSButton { objectName: "changeKeyButton"; text: "변경"; Layout.preferredWidth: 72; Layout.preferredHeight: 36; Layout.alignment: Qt.AlignTop; onClicked: { keyChoice.currentIndex = preview.transitionKey === "오른쪽 Alt" ? 0 : 1; keyDialog.open() } }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    SettingRow { width: parent.width; height: 40; inset: 0; color: "transparent"; label: "전환 방식"; value: "Mac의 기존 설정 사용" }
                    KSText { width: parent.width; text: "Mac의 단축키와 내장 키보드 설정을 유지합니다."; font.pixelSize: Theme.fontCaption; color: Theme.muted; wrapMode: Text.WordWrap; height: 20 }
                }
            }
            Rectangle {
                width: parent.width; height: 150; radius: Theme.r8; color: Theme.surface
                Column {
                    anchors.fill: parent; anchors.margins: 24; spacing: 12
                    KSText { text: "입력 확인"; font.pixelSize: Theme.fontSection; font.bold: true; height: 26 }
                    RowLayout {
                        width: parent.width; height: 40; spacing: 12
                        TextField {
                            id: input
                            objectName: "inputCheckField"
                            Layout.fillWidth: true; Layout.preferredHeight: 40
                            placeholderText: "여기에 한글과 영어를 입력하세요"
                            placeholderTextColor: Theme.muted
                            color: Theme.text
                            selectByMouse: true
                            leftPadding: 8; rightPadding: 8
                            Accessible.name: "한글과 영어 입력 확인"
                            background: Rectangle { radius: Theme.r8; color: Theme.field; border.color: input.activeFocus ? Theme.accent : Theme.border; border.width: input.activeFocus ? 2 : 1 }
                        }
                        KSButton {
                            objectName: "inputCheckButton"
                            primary: true
                            text: preview.inputCheckActive ? "입력 확인 종료" : "입력 확인 시작"
                            onClicked: {
                                preview.inputCheckActive = !preview.inputCheckActive
                                if (preview.inputCheckActive) input.forceActiveFocus()
                            }
                        }
                    }
                    KSText { width: parent.width; text: "입력은 이 창에만 표시되며 저장하거나 전송하지 않습니다."; font.pixelSize: Theme.fontCaption; color: Theme.muted; wrapMode: Text.WordWrap; height: 20 }
                }
            }
            Column {
                width: parent.width; spacing: 8
                AbstractButton {
                    id: mappingButton
                    objectName: "mappingButton"
                    width: parent.width; height: 56
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: preview.mappingExpanded ? "단축키 매핑 접기" : "단축키 매핑 펼치기"
                    onClicked: preview.mappingExpanded = !preview.mappingExpanded
                    contentItem: SettingRow {
                        label: "단축키 매핑"
                        value: preview.ctrlToCommand ? "Ctrl → Command" : "원래 키 유지"
                        border.color: mappingButton.visualFocus ? Theme.white : "transparent"
                        border.width: mappingButton.visualFocus ? 2 : 0
                    }
                }
                Rectangle {
                    width: parent.width; height: visible ? 90 : 0
                    visible: preview.mappingExpanded
                    color: Theme.surface; radius: Theme.r8
                    Switch {
                        objectName: "mappingSwitch"
                        anchors.left: parent.left; anchors.leftMargin: 16; anchors.top: parent.top; anchors.topMargin: 8
                        text: "Mac에서 Ctrl을 Command로 사용"
                        checked: preview.ctrlToCommand
                        onToggled: preview.ctrlToCommand = checked
                    }
                    KSText { anchors.left: parent.left; anchors.leftMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: "미리보기 값만 변경됩니다. 실제 키는 변환하지 않습니다."; font.pixelSize: 12; color: Theme.muted }
                }
            }
            KSText { objectName: "previewNotice"; width: parent.width; text: "미리보기 · 실제 기기 연결 및 시스템 설정 변경 없음"; font.pixelSize: Theme.fontCaption; color: Theme.muted; wrapMode: Text.WordWrap; height: 20 }
        }
    }

    Dialog {
        id: keyDialog
        objectName: "keyDialog"
        anchors.centerIn: parent
        width: 410
        modal: true
        title: "전환 키 선택"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { standardButton(Dialog.Ok).text = "선택"; standardButton(Dialog.Cancel).text = "취소" }
        onAccepted: preview.transitionKey = keyChoice.currentIndex === 0 ? "오른쪽 Alt" : "한/영 키"
        ColumnLayout {
            width: parent.width; spacing: 16
            KSText { text: "미리보기에서 사용할 키를 선택하세요." }
            ComboBox { id: keyChoice; objectName: "keyChoice"; Layout.fillWidth: true; model: ["오른쪽 Alt", "한/영 키"] }
            KSText { Layout.fillWidth: true; text: "Mac의 입력 소스 단축키는 변경하지 않습니다."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
    }
    Dialog {
        id: info
        anchors.centerIn: parent; width: 430; modal: true; title: "화면 구현 범위"; standardButtons: Dialog.Close
        onOpened: standardButton(Dialog.Close).text = "닫기"
        KSText { width: parent.width; wrapMode: Text.WordWrap; text: "이번 QML 미리보기는 키보드 설정을 구현했습니다. 메인 화면과 메뉴바 패널은 같은 Figma 파일에서 관리합니다." }
    }
    Dialog {
        id: targetInfo
        anchors.centerIn: parent; width: 430; modal: true; title: "미리보기 기기"; standardButtons: Dialog.Close
        onOpened: standardButton(Dialog.Close).text = "닫기"
        KSText { width: parent.width; wrapMode: Text.WordWrap; text: "Windows PC → MacBook Pro는 디자인 확인용 예시입니다. 실제 기기를 찾거나 연결하지 않습니다." }
    }
}
