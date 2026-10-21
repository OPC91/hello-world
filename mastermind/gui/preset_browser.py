"""Preset browser: search, category filter, favourites, apply, save, import/export."""

from __future__ import annotations

import html

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from mastermind.presets.schema import CATEGORIES, Preset
from mastermind.presets.store import FAVORITES, PresetLibrary

ALL = "All categories"
ID_ROLE = Qt.ItemDataRole.UserRole


def describe(p: Preset) -> str:
    t = p.target
    widths = " / ".join(f"{w:g}" for w in t.band_width)
    mono = f"{t.mono_freq:g} Hz" if t.mono_freq else "off"
    rows = [
        ("Target loudness", f"{t.lufs:g} LUFS"),
        ("Ceiling", f"{t.true_peak_ceiling:g} dBTP"),
        ("Crest factor", f"{t.crest_db:g} dB"),
        ("Loudness range", f"{t.lra_lu:g} LU" if t.lra_lu is not None else "—"),
        ("Width L/LM/HM/H", f"{widths} %"),
        ("Mono below", mono),
    ]
    table = "".join(f"<tr><td style='color:#8b949e;padding-right:8px'>{k}</td><td>{v}</td></tr>" for k, v in rows)
    tags = ", ".join(p.tags)
    src = "Factory" if p.factory else f"User ({html.escape(p.author)})"
    return (
        f"<h3>{html.escape(p.name)}</h3><p><i>{html.escape(p.category)}</i> · {src}</p>"
        f"<p>{html.escape(p.description)}</p>"
        + (f"<p style='color:#8b949e'>{html.escape(p.notes)}</p>" if p.notes else "")
        + f"<table>{table}</table>"
        + (f"<p style='color:#8b949e'>Tags: {html.escape(tags)}</p>" if tags else "")
    )


class SavePresetDialog(QDialog):
    def __init__(self, parent=None, default_name: str = "", default_category: str = "User"):
        super().__init__(parent)
        self.setWindowTitle("Save preset")
        form = QFormLayout(self)
        self.name = QLineEdit(default_name)
        self.category = QComboBox()
        self.category.setEditable(True)
        self.category.addItems(list(CATEGORIES))
        self.category.setCurrentText(default_category)
        self.description = QLineEdit()
        form.addRow("Name", self.name)
        form.addRow("Category", self.category)
        form.addRow("Description", self.description)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        form.addRow(bb)

    def values(self) -> tuple[str, str, str]:
        return (
            self.name.text().strip() or "Untitled",
            self.category.currentText().strip() or "User",
            self.description.text(),
        )


class PresetBrowser(QWidget):
    apply_requested = Signal(object, bool)  # Preset, auto_gain
    save_requested = Signal()

    def __init__(self, library: PresetLibrary):
        super().__init__()
        self.library = library
        lay = QVBoxLayout(self)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search presets (name, genre, tags)…")
        self.search.setClearButtonEnabled(True)
        self.category = QComboBox()
        self.list = QListWidget()
        self.detail = QTextBrowser()
        self.detail.setMinimumHeight(170)
        self.auto_gain = QCheckBox("Auto-gain to preset loudness")
        self.auto_gain.setChecked(True)
        self.auto_gain.setToolTip("After applying, fit the limiter gain so this track hits the preset's LUFS target.")
        lay.addWidget(self.search)
        lay.addWidget(self.category)
        lay.addWidget(self.list, 1)
        lay.addWidget(self.detail)
        lay.addWidget(self.auto_gain)

        grid = QGridLayout()
        self.btn_apply = QPushButton("Apply")
        self.btn_fav = QPushButton("★ Favourite")
        self.btn_save = QPushButton("Save current…")
        self.btn_import = QPushButton("Import…")
        self.btn_export = QPushButton("Export…")
        self.btn_delete = QPushButton("Delete")
        for i, b in enumerate(
            (self.btn_apply, self.btn_fav, self.btn_save, self.btn_import, self.btn_export, self.btn_delete)
        ):
            grid.addWidget(b, i // 2, i % 2)
        lay.addLayout(grid)

        self.search.textChanged.connect(self.refresh)
        self.category.currentIndexChanged.connect(self.refresh)
        self.list.currentItemChanged.connect(self._selected)
        self.list.itemDoubleClicked.connect(lambda _i: self._apply())
        self.btn_apply.clicked.connect(self._apply)
        self.btn_fav.clicked.connect(self._toggle_fav)
        self.btn_save.clicked.connect(self.save_requested.emit)
        self.btn_import.clicked.connect(self._import)
        self.btn_export.clicked.connect(self._export)
        self.btn_delete.clicked.connect(self._delete)
        self._fill_categories()
        self.refresh()

    # ---- list -------------------------------------------------------------
    def _fill_categories(self) -> None:
        cur = self.category.currentText()
        self.category.blockSignals(True)
        self.category.clear()
        self.category.addItems([ALL, FAVORITES, *self.library.categories()])
        if cur:
            self.category.setCurrentText(cur)
        self.category.blockSignals(False)

    def refresh(self, *_args, select: str | None = None) -> None:
        select = select or self.selected_id()
        cat = self.category.currentText()
        presets = self.library.search(self.search.text(), None if cat == ALL else cat)
        self.list.blockSignals(True)
        self.list.clear()
        for p in presets:
            star = "★ " if self.library.is_favorite(p.id) else "   "
            item = QListWidgetItem(f"{star}{p.name}   ·   {p.category}")
            item.setData(ID_ROLE, p.id)
            if not p.factory:
                item.setForeground(Qt.GlobalColor.cyan)
            self.list.addItem(item)
            if p.id == select:
                self.list.setCurrentItem(item)
        self.list.blockSignals(False)
        self._selected()

    def selected_id(self) -> str | None:
        item = self.list.currentItem()
        return item.data(ID_ROLE) if item else None

    def selected(self) -> Preset | None:
        pid = self.selected_id()
        return self.library.get(pid) if pid else None

    def select(self, preset_id: str) -> None:
        self.search.clear()
        self.category.setCurrentText(ALL)
        self.refresh(select=preset_id)

    def _selected(self, *_args) -> None:
        p = self.selected()
        self.detail.setHtml(describe(p) if p else "<p>No preset selected.</p>")
        for b in (self.btn_apply, self.btn_fav, self.btn_export):
            b.setEnabled(p is not None)
        self.btn_delete.setEnabled(p is not None and not p.factory)
        if p:
            self.btn_fav.setText("☆ Unfavourite" if self.library.is_favorite(p.id) else "★ Favourite")

    # ---- actions ------------------------------------------------------------
    def _apply(self) -> None:
        p = self.selected()
        if p:
            self.apply_requested.emit(p, self.auto_gain.isChecked())

    def _toggle_fav(self) -> None:
        pid = self.selected_id()
        if pid:
            self.library.toggle_favorite(pid)
            self.refresh(select=pid)

    def _import(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Import preset", "", "MasterMind preset (*.json)")
        if path:
            self.import_path(path)

    def import_path(self, path: str) -> Preset | None:
        try:
            p = self.library.import_file(path)
        except Exception as e:
            QMessageBox.warning(self, "Import failed", f"{path} is not a valid preset:\n{e}")
            return None
        self._fill_categories()
        self.select(p.id)
        return p

    def _export(self) -> None:
        p = self.selected()
        if not p:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Export preset", p.id.replace("/", "-") + ".json", "JSON (*.json)")
        if path:
            self.library.export(p.id, path)

    def _delete(self) -> None:
        p = self.selected()
        if p and not p.factory:
            if (
                QMessageBox.question(self, "Delete preset", f"Delete user preset '{p.name}'?")
                == QMessageBox.StandardButton.Yes
            ):
                self.library.delete_user(p.id)
                self.refresh()

    def add_saved(self, preset: Preset) -> None:
        self._fill_categories()
        self.select(preset.id)
