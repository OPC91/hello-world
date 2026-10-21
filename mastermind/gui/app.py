"""Entry point for the ``mastermind`` GUI."""

from __future__ import annotations

import sys


def main(argv: list[str] | None = None) -> int:
    import pyqtgraph as pg
    from PySide6.QtWidgets import QApplication

    from mastermind.gui.main_window import MainWindow

    argv = sys.argv if argv is None else argv
    app = QApplication.instance() or QApplication(argv)
    app.setApplicationName("MasterMind")
    pg.setConfigOptions(antialias=True)
    win = MainWindow()
    win.show()
    if len(argv) > 1:
        win.open_file(argv[1])
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
