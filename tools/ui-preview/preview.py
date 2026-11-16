"""Isolated Qt 6 host for QML UI development; no production engine imports."""
import argparse
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent


def create_preview(offscreen=False, width=1000, height=720):
    if offscreen:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        os.environ["QT_QUICK_BACKEND"] = "software"
    os.environ["QML_DISABLE_DISK_CACHE"] = "1"
    os.environ["QT_QUICK_CONTROLS_STYLE"] = "Basic"
    from PySide6.QtCore import QUrl
    from PySide6.QtGui import QFont, QFontDatabase, QGuiApplication
    from PySide6.QtQml import QQmlApplicationEngine
    from PySide6.QtQuick import QQuickWindow
    app = QGuiApplication(["keystitch-ui-preview"])
    app.setOrganizationName("KeyStitchPreview")
    app.setApplicationName("KeyStitch UI Preview")
    font_id = QFontDatabase.addApplicationFont(str(ROOT / "assets/fonts/NotoSansKR.ttf"))
    if font_id < 0:
        raise RuntimeError("Could not load the bundled Noto Sans KR font")
    app.setFont(QFont("Noto Sans KR", 14))
    engine = QQmlApplicationEngine()
    engine.load(QUrl.fromLocalFile(str(ROOT / "qml/Main.qml")))
    if not engine.rootObjects():
        raise RuntimeError("QML did not create a window")
    window = engine.rootObjects()[0]
    if not isinstance(window, QQuickWindow):
        raise RuntimeError("QML root is not a Qt Quick window")
    window.setWidth(width)
    window.setHeight(height)
    return app, engine, window


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render", type=Path, help="Render offscreen and exit; does not open a desktop window")
    parser.add_argument("--width", type=int, default=1000)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()
    app, engine, window = create_preview(bool(args.render), args.width, args.height)
    if args.render:
        from PySide6.QtCore import QTimer
        def capture():
            try:
                args.render.parent.mkdir(parents=True, exist_ok=True)
                if not window.grabWindow().save(str(args.render)):
                    raise RuntimeError("Could not save the offscreen render")
            except Exception as exc:
                print(str(exc), file=sys.stderr)
                app.exit(1)
            else:
                app.quit()
        QTimer.singleShot(300, capture)
        QTimer.singleShot(10000, lambda: app.exit(2))
    sys.exit(app.exec())
