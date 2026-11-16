"""In-process offscreen UI checks. These do not synthesize OS keyboard events."""
import sys
import unittest

from preview import ROOT, create_preview

app, engine, window = create_preview(offscreen=True)
from PySide6.QtCore import QObject, QMetaObject, QPointF, Qt
from PySide6.QtGui import QInputMethodEvent
from PySide6.QtTest import QTest


def find(name):
    obj = window.findChild(QObject, name)
    if obj is None:
        raise AssertionError("Missing control: " + name)
    return obj


def click(name):
    item = find(name)
    point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point)
    app.processEvents()


def invoke(obj, method):
    assert QMetaObject.invokeMethod(obj, method)
    app.processEvents()


def snapshot(name):
    QTest.qWait(80)
    out = ROOT / "artifacts" / name
    out.parent.mkdir(exist_ok=True)
    assert window.grabWindow().save(str(out))


class KeyboardPreviewTests(unittest.TestCase):
    def setUp(self):
        window.setWidth(1000)
        window.setHeight(720)
        self.model = find("previewModel")
        invoke(self.model, "reset")
        invoke(find("keyDialog"), "close")
        find("settingsSearch").setProperty("text", "")
        find("inputCheckField").setProperty("text", "")
        find("contentScroll").property("contentItem").setProperty("contentY", 0)
        QTest.qWait(50)

    def test_pause_and_resume_only_changes_preview(self):
        click("pauseButton")
        self.assertTrue(self.model.property("paused"))
        self.assertEqual(find("pauseButton").property("text"), "재개")
        snapshot("keyboard-paused.png")
        click("pauseButton")
        self.assertFalse(self.model.property("paused"))

    def test_key_choice_cancel_and_accept(self):
        click("changeKeyButton")
        self.assertTrue(find("keyDialog").property("visible"))
        find("keyChoice").setProperty("currentIndex", 1)
        snapshot("keyboard-key-dialog.png")
        invoke(find("keyDialog"), "reject")
        self.assertEqual(self.model.property("transitionKey"), "오른쪽 Alt")
        click("changeKeyButton")
        find("keyChoice").setProperty("currentIndex", 1)
        invoke(find("keyDialog"), "accept")
        self.assertEqual(self.model.property("transitionKey"), "한/영 키")

    def test_input_commit_focus_and_preservation(self):
        click("inputCheckButton")
        field = find("inputCheckField")
        self.assertTrue(field.property("activeFocus"))
        # Delivery to this offscreen control only: NOT a real IME switching test.
        commit = QInputMethodEvent()
        commit.setCommitString("가나")
        app.sendEvent(field, commit)
        for key in (Qt.Key_A, Qt.Key_B, Qt.Key_C):
            QTest.keyClick(window, key)
        self.assertEqual(field.property("text"), "가나abc")
        click("inputCheckButton")
        click("inputCheckButton")
        self.assertEqual(field.property("text"), "가나abc")

    def test_mapping_expands_and_changes_local_model(self):
        click("mappingButton")
        self.assertTrue(self.model.property("mappingExpanded"))
        control = find("mappingSwitch")
        # Directly exercise the control's normal keyboard path after focus.
        control.forceActiveFocus()
        QTest.keyClick(window, Qt.Key_Space)
        self.assertFalse(self.model.property("ctrlToCommand"))
        snapshot("keyboard-expanded.png")

    def test_tab_focus_and_compact_layout(self):
        find("settingsSearch").forceActiveFocus()
        QTest.keyClick(window, Qt.Key_Tab)
        self.assertIsNot(window.activeFocusItem(), find("settingsSearch"))
        window.setWidth(880)
        window.setHeight(620)
        QTest.qWait(80)
        field = find("inputCheckField")
        self.assertGreater(field.width(), 250)
        self.assertLess(field.mapToScene(QPointF(field.width(), 0)).x(), window.width())
        snapshot("keyboard-compact.png")
        flickable = find("contentScroll").property("contentItem")
        bottom = flickable.property("contentHeight") - flickable.property("height")
        self.assertGreater(bottom, 0)
        flickable.setProperty("contentY", bottom)
        app.processEvents()
        note = find("previewNotice")
        self.assertLessEqual(note.mapToScene(QPointF(0, note.height())).y(), window.height())
        snapshot("keyboard-compact-scrolled.png")


if __name__ == "__main__":
    try:
        result = unittest.main(exit=False).result
    finally:
        window.close()
    sys.exit(0 if result.wasSuccessful() else 1)
