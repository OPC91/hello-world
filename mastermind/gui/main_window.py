"""Phase 1 main window: load, transport, chain editing and export.

All chain mutations go through ``PlaybackEngine.post`` (thread-safe command
queue); the window only *reads* processor state to populate controls. Meter
snapshots are polled from the engine's SnapshotQueue on a ~30 fps timer.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np
from PySide6.QtCore import QObject, Qt, QThread, QTimer, Signal
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDockWidget,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QProgressDialog,
    QSlider,
    QSplitter,
    QTabWidget,
    QToolBar,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.analysis import FileAnalysis, analyze
from mastermind.dsp.metering.targets import LOUDNESS_TARGETS
from mastermind.engine.commands import Reorder, SetBypass, SetParam
from mastermind.engine.offline import render, render_to_file
from mastermind.engine.realtime import PlaybackEngine
from mastermind.gui.chain_view import ChainListWidget
from mastermind.gui.meters.analysis_panel import AnalysisPanel
from mastermind.gui.meters.meter_panel import MeterPanel
from mastermind.gui.meters.spectrum_panel import SpectrumPanel
from mastermind.gui.meters.stereo_panel import StereoPanel
from mastermind.gui.param_panel import ParamPanel
from mastermind.gui.preset_browser import PresetBrowser, SavePresetDialog
from mastermind.io.audio_file import SUPPORTED_EXTENSIONS, AudioData, load_audio
from mastermind.io.export import BIT_DEPTHS
from mastermind.matching.loudness_fit import LoudnessFit, fit_limiter_drive
from mastermind.presets.schema import SPECTRUM_NORM_RANGE, Preset, TargetProfile
from mastermind.presets.store import PresetLibrary

GUI_FPS = 30


def fmt_time(seconds: float) -> str:
    m, s = divmod(max(0.0, seconds), 60)
    return f"{int(m):d}:{s:05.2f}"


class ExportWorker(QObject):
    progress = Signal(int)
    finished = Signal(str)
    failed = Signal(str)

    def __init__(self, chain: MasteringChain, audio: AudioData, path: str, bits: str, dither: bool):
        super().__init__()
        self.chain, self.audio, self.path, self.bits, self.dither = chain, audio, path, bits, dither
        self.cancelled = False

    def run(self) -> None:
        try:
            render_to_file(
                self.chain,
                self.audio.samples,
                self.audio.sample_rate,
                self.path,
                self.bits,  # type: ignore[arg-type]
                dither=self.dither,
                progress=lambda f: (self.progress.emit(int(f * 100)), not self.cancelled)[1],
            )
            self.finished.emit(self.path)
        except InterruptedError:
            self.failed.emit("Export cancelled.")
        except Exception as e:  # surfaced to the user
            self.failed.emit(f"Export failed: {e}")


class AnalysisWorker(QObject):
    """Analyses the input, renders the chain offline and analyses the output."""

    finished = Signal(object, object)
    failed = Signal(str)

    def __init__(self, chain: MasteringChain, audio: AudioData):
        super().__init__()
        self.chain, self.audio = chain, audio

    def run(self) -> None:
        try:
            a_in = analyze(self.audio.samples, self.audio.sample_rate)
            y = render(self.chain, self.audio.samples, self.audio.sample_rate)
            a_out = analyze(y, self.audio.sample_rate)
            self.finished.emit(a_in, a_out)
        except Exception as e:  # surfaced to the user
            self.failed.emit(f"Analysis failed: {e}")


class LoudnessFitWorker(QObject):
    finished = Signal(object)
    failed = Signal(str)

    def __init__(self, chain: MasteringChain, audio: AudioData, target: float):
        super().__init__()
        self.chain, self.audio, self.target = chain, audio, target

    def run(self) -> None:
        try:
            self.finished.emit(fit_limiter_drive(self.chain, self.audio.samples, self.audio.sample_rate, self.target))
        except Exception as e:  # surfaced to the user
            self.failed.emit(f"Auto-gain failed: {e}")


def run_in_thread(parent: QObject, worker: QObject, finished_slot, failed_slot) -> QThread:
    """Move ``worker`` (with run/finished/failed) to a new QThread and start it."""
    thread = QThread(parent)
    worker.moveToThread(thread)
    thread.started.connect(worker.run)
    worker.finished.connect(finished_slot)
    worker.failed.connect(failed_slot)
    worker.finished.connect(thread.quit)
    worker.failed.connect(thread.quit)
    thread.finished.connect(worker.deleteLater)
    thread.start()
    return thread


class ExportDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Export options")
        form = QFormLayout(self)
        self.bits = QComboBox()
        self.bits.addItems([{"16": "16-bit", "24": "24-bit", "32f": "32-bit float"}[b] for b in BIT_DEPTHS])
        self.bits.setCurrentIndex(1)
        self.dither = QCheckBox("TPDF dither (integer formats)")
        self.dither.setChecked(True)
        form.addRow("Format", self.bits)
        form.addRow("", self.dither)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        form.addRow(bb)

    def values(self) -> tuple[str, bool]:
        return BIT_DEPTHS[self.bits.currentIndex()], self.dither.isChecked()


class MainWindow(QMainWindow):
    def __init__(self, use_device: bool = True, library: PresetLibrary | None = None):
        super().__init__()
        self.setWindowTitle("MasterMind")
        self.resize(1400, 860)
        self.setAcceptDrops(True)
        self.engine = PlaybackEngine(use_device=use_device)
        self.audio: AudioData | None = None
        self._export_thread: QThread | None = None
        self._threads: list[QThread] = []
        self.library = library or PresetLibrary()
        self.current_preset: Preset | None = None
        self._reference: tuple[np.ndarray, np.ndarray] | None = None
        self.last_fit: LoudnessFit | None = None

        self._build_toolbar()
        self.chain_list = ChainListWidget(self._on_reorder, self._on_bypass)
        self.chain_list.currentRowChanged.connect(self._on_select)
        self.params = ParamPanel(self._on_param)

        self.spectrum = SpectrumPanel()
        self.stereo = StereoPanel()
        self.analysis = AnalysisPanel()
        self.analysis.analyze_requested.connect(self.run_analysis)
        self.tabs = QTabWidget()
        self.tabs.addTab(self.params, "Parameters")
        self.tabs.addTab(self.spectrum, "Spectrum")
        self.tabs.addTab(self.stereo, "Stereo")
        self.tabs.addTab(self.analysis, "Analysis")

        self.browser = PresetBrowser(self.library)
        self.browser.apply_requested.connect(self.apply_preset)
        self.browser.save_requested.connect(self.save_preset_dialog)
        self.left_tabs = QTabWidget()
        self.left_tabs.addTab(self.chain_list, "Chain")
        self.left_tabs.addTab(self.browser, "Presets")

        split = QSplitter()
        split.addWidget(self.left_tabs)
        split.addWidget(self.tabs)
        split.setSizes([300, 700])

        self.meters = MeterPanel()
        self.meters.loudness.reset_requested.connect(self.reset_meters)
        self.meters.group.buttonToggled.connect(lambda *_: self.stereo.clear())
        dock = QDockWidget("Meters", self)
        dock.setObjectName("meters")
        dock.setWidget(self.meters)
        dock.setFeatures(
            QDockWidget.DockWidgetFeature.DockWidgetMovable | QDockWidget.DockWidgetFeature.DockWidgetFloatable
        )
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, dock)
        self._dock = dock
        self._docks_sized = False
        self._analysis_thread: QThread | None = None
        self._play_clock = 0.0

        self.pos_slider = QSlider(Qt.Orientation.Horizontal)
        self.pos_slider.setRange(0, 1000)
        self.pos_slider.sliderReleased.connect(self._on_seek)
        self.time_label = QLabel("0:00.00 / 0:00.00")
        transport = QHBoxLayout()
        transport.addWidget(self.pos_slider, 1)
        transport.addWidget(self.time_label)

        root = QWidget()
        lay = QVBoxLayout(root)
        lay.addLayout(transport)
        lay.addWidget(split, 1)
        self.setCentralWidget(root)

        self.chain_list.set_chain(self.engine.chain)
        self.chain_list.setCurrentRow(0)

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(1000 // GUI_FPS)

    # ---- UI construction ---------------------------------------------
    def _build_toolbar(self) -> None:
        tb = QToolBar("Transport")
        self.addToolBar(tb)
        self.act_open = QAction("Open…", self, shortcut=QKeySequence.StandardKey.Open, triggered=self.open_dialog)
        self.act_play = QAction("Play", self, shortcut=QKeySequence(Qt.Key.Key_Space), triggered=self.toggle_play)
        self.act_stop = QAction("Stop", self, triggered=self.stop)
        self.act_export = QAction("Export…", self, shortcut=QKeySequence("Ctrl+E"), triggered=self.export_dialog)
        self.loop_box = QCheckBox("Loop")
        self.loop_box.toggled.connect(lambda v: setattr(self.engine, "loop", v))
        for a in (self.act_open, self.act_play, self.act_stop, self.act_export):
            tb.addAction(a)
        tb.addWidget(self.loop_box)
        self._build_ab_toolbar()

    def _build_ab_toolbar(self) -> None:
        tb = QToolBar("A/B")
        tb.setObjectName("ab")
        self.addToolBar(tb)
        self.btn_a = QToolButton(text="A", checkable=True, checked=True)
        self.btn_b = QToolButton(text="B", checkable=True)
        grp = QButtonGroup(self)
        grp.setExclusive(True)
        grp.addButton(self.btn_a)
        grp.addButton(self.btn_b)
        self.btn_a.clicked.connect(lambda: self.set_ab("A"))
        self.btn_b.clicked.connect(lambda: self.set_ab("B"))
        self.btn_orig = QToolButton(text="Original", checkable=True)
        self.btn_orig.setToolTip("Monitor the unprocessed input (latency-aligned)")
        self.btn_orig.toggled.connect(lambda v: setattr(self.engine, "monitor_original", v))
        self.match_box = QCheckBox("Match loudness")
        self.match_box.setChecked(True)
        self.match_box.setToolTip("Turn the monitored path down to the quietest of A / B / Original (monitoring only)")
        self.match_box.toggled.connect(lambda v: setattr(self.engine, "loudness_match", v))
        act_ab = QAction("Copy A→B", self, triggered=lambda: self.copy_ab("A", "B"))
        act_ba = QAction("Copy B→A", self, triggered=lambda: self.copy_ab("B", "A"))
        tb.addWidget(QLabel(" Compare: "))
        for w in (self.btn_a, self.btn_b, self.btn_orig):
            tb.addWidget(w)
        tb.addAction(act_ab)
        tb.addAction(act_ba)
        tb.addWidget(self.match_box)
        self.ab_label = QLabel("")
        tb.addWidget(self.ab_label)

    # ---- file handling ---------------------------------------------------
    def open_dialog(self) -> None:
        exts = " ".join(f"*{e}" for e in SUPPORTED_EXTENSIONS)
        path, _ = QFileDialog.getOpenFileName(self, "Open audio", "", f"Audio ({exts})")
        if path:
            self.open_file(path)

    def open_file(self, path: str | Path) -> None:
        try:
            self.audio = load_audio(path)
        except Exception as e:
            QMessageBox.warning(self, "Open failed", f"Could not open {path}:\n{e}")
            return
        self.engine.load(self.audio.samples, self.audio.sample_rate)
        self.meters.loudness.clear_history()
        self.stereo.clear()
        self._play_clock = 0.0
        dev = "" if self.engine.has_device else "  [no audio device: silent playback]"
        self.statusBar().showMessage(
            f"{Path(path).name} — {self.audio.sample_rate} Hz, {fmt_time(self.audio.duration)}, "
            f"chain latency {self.engine.chain.latency_samples} samples{dev}"
        )
        self.setWindowTitle(f"MasterMind — {Path(path).name}")

    def dragEnterEvent(self, e) -> None:
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e) -> None:
        for url in e.mimeData().urls():
            if Path(url.toLocalFile()).suffix.lower() in SUPPORTED_EXTENSIONS:
                self.open_file(url.toLocalFile())
                break

    # ---- transport ---------------------------------------------------------
    def toggle_play(self) -> None:
        if self.engine.playing:
            self.engine.pause()
        else:
            self.engine.play()

    def stop(self) -> None:
        self.engine.pause()
        self.engine.seek(0)

    def _on_seek(self) -> None:
        if self.engine.duration_samples:
            self.engine.seek(self.pos_slider.value() / 1000 * self.engine.duration_samples)

    # ---- chain editing -----------------------------------------------------
    def _on_select(self, row: int) -> None:
        if row < 0:
            self.params.show_processor(None)
            return
        type_id = self.chain_list.item(row).data(Qt.ItemDataRole.UserRole)
        self.params.show_processor(self.engine.chain.find(type_id))

    def _on_param(self, type_id: str, pid: str, value: Any) -> None:
        self.engine.post(SetParam(type_id, pid, value))

    def _on_bypass(self, type_id: str, bypass: bool) -> None:
        self.engine.post(SetBypass(type_id, bypass))

    def _on_reorder(self, order: list[str]) -> None:
        self.engine.post(Reorder(tuple(order)))

    # ---- A/B ------------------------------------------------------------------
    def refresh_chain_view(self) -> None:
        row = max(self.chain_list.currentRow(), 0)
        self.chain_list.set_chain(self.engine.chain)
        self.chain_list.setCurrentRow(min(row, self.chain_list.count() - 1))
        self._on_select(self.chain_list.currentRow())

    def set_ab(self, key: str) -> None:
        if key == "B":
            self.engine.enable_b()
        self.engine.set_active(key)
        (self.btn_a if key == "A" else self.btn_b).setChecked(True)
        self.refresh_chain_view()
        self.statusBar().showMessage(f"Editing and monitoring {key}", 3000)

    def copy_ab(self, src: str, dst: str) -> None:
        if src not in self.engine.chains:
            return
        self.engine.copy_chain(src, dst)
        if dst == self.engine.active:
            self.refresh_chain_view()
        self.statusBar().showMessage(f"Copied {src} → {dst}", 3000)

    def _update_ab_label(self, ab: dict) -> None:
        loud = ab["loudness"]
        names = {"orig": "Orig"}
        parts = [f"{names.get(k, k)} {loud[k]:.1f}" for k in ab["paths"] if k in loud and np.isfinite(loud[k])]
        gain = f"  ·  monitor {ab['match_gain_db']:+.1f} dB" if ab["loudness_match"] else ""
        self.ab_label.setText(("  LUFS: " + "  ".join(parts) + gain) if parts else "")

    # ---- presets --------------------------------------------------------------
    def apply_preset(self, preset: Preset, auto_gain: bool = True) -> QThread | None:
        """Load a preset into the active chain; optionally fit the limiter to its loudness."""
        self.engine.replace_chain(preset.build_chain())
        self.current_preset = preset
        self.refresh_chain_view()
        self.set_target_lufs(preset.target.lufs)
        f, db = preset.target.spectrum_array()
        self._reference = (f, db)
        self.statusBar().showMessage(f"Applied preset {preset.name}", 5000)
        if auto_gain and self.audio is not None:
            return self.auto_gain(preset.target.lufs)
        return None

    def auto_gain(self, target: float) -> QThread | None:
        if self.audio is None:
            return None
        worker = LoudnessFitWorker(self.engine.snapshot_chain(), self.audio, target)
        self.statusBar().showMessage(f"Auto-gain: fitting limiter to {target:g} LUFS…")
        th = run_in_thread(self, worker, self._fit_finished, self._fit_failed)
        self._threads.append(th)
        self._fit_worker = worker
        return th

    def _fit_finished(self, fit: LoudnessFit) -> None:
        self.last_fit = fit
        self.engine.post(SetParam("limiter", "drive_db", fit.drive_db))
        if self.params.type_id == "limiter":
            self.params.show_processor(self.engine.chain.find("limiter"))
        note = "" if fit.converged else " (target not reachable; closest setting applied)"
        self.statusBar().showMessage(
            f"Auto-gain: limiter gain {fit.drive_db:+.1f} dB → {fit.achieved_lufs:.1f} LUFS{note}", 8000
        )

    def _fit_failed(self, msg: str) -> None:
        self.statusBar().showMessage(msg, 8000)

    def set_target_lufs(self, lufs: float) -> None:
        lp = self.meters.loudness
        names = list(LOUDNESS_TARGETS)
        match = [n for n, v in LOUDNESS_TARGETS.items() if v == lufs]
        if match:
            lp.target_box.setCurrentIndex(names.index(match[0]))
        else:
            lp.target_box.setCurrentIndex(names.index("Custom"))
            lp.custom.setValue(lufs)

    def current_target_profile(self) -> TargetProfile:
        if self.current_preset is not None:
            return self.current_preset.target.model_copy()
        im = self.engine.chain.find("imager")
        return TargetProfile(
            lufs=self.meters.loudness.target,
            true_peak_ceiling=self.ceiling,
            band_width=[float(im.get_param(f"width_b{i}")) for i in range(1, 5)],
            mono_freq=float(im.get_param("mono_freq")) if im.get_param("mono_maker") else 0.0,
        )

    def save_preset_dialog(self) -> None:
        base = self.current_preset
        dlg = SavePresetDialog(self, f"{base.name} (custom)" if base else "", "User")
        if dlg.exec() == QDialog.DialogCode.Accepted:
            self.save_current_as(*dlg.values())

    def save_current_as(self, name: str, category: str = "User", description: str = "") -> Preset:
        preset = Preset(
            id="user/new",
            name=name,
            category=category,
            description=description,
            notes=f"Based on {self.current_preset.name}." if self.current_preset else "",
            tags=list(self.current_preset.tags) if self.current_preset else [],
            chain=self.engine.snapshot_chain().to_dict(),
            target=self.current_target_profile(),
        )
        saved = self.library.save_user(preset)
        self.browser.add_saved(saved)
        self.statusBar().showMessage(f"Saved preset {saved.name}", 5000)
        return saved

    def _reference_on_grid(self, grid: np.ndarray, out_db: np.ndarray | None):
        """Preset target curve interpolated to the analyser grid and level-aligned to the output."""
        if self._reference is None or out_db is None:
            return None
        f, db = self._reference
        ref = np.interp(np.log2(grid), np.log2(f), db)
        lo, hi = SPECTRUM_NORM_RANGE
        sel = (grid >= lo) & (grid <= hi)
        offset = 10 * np.log10(np.mean(10 ** (out_db[sel] / 10))) - 10 * np.log10(np.mean(10 ** (ref[sel] / 10)))
        return ref + offset

    # ---- meters / analysis ---------------------------------------------------
    @property
    def ceiling(self) -> float:
        return float(self.engine.chain.find("limiter").get_param("ceiling_db"))

    def reset_meters(self) -> None:
        self.engine.reset_meters()
        self.meters.loudness.clear_history()
        self.stereo.clear()

    def run_analysis(self) -> QThread | None:
        """Full-file analysis of the input and the rendered output, on a background thread."""
        if self.audio is None:
            self.analysis.set_error("Open a file first.")
            return None
        worker = AnalysisWorker(self.engine.snapshot_chain(), self.audio)
        thread = QThread(self)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.finished.connect(self._analysis_finished)
        worker.failed.connect(self._analysis_failed)
        worker.finished.connect(thread.quit)
        worker.failed.connect(thread.quit)
        thread.finished.connect(worker.deleteLater)
        self._analysis_worker = worker
        self._analysis_thread = thread
        self.analysis.set_busy("Analyzing input, rendering and analyzing output…")
        thread.start()
        return thread

    def _analysis_finished(self, a_in: FileAnalysis, a_out: FileAnalysis) -> None:
        self.analysis.set_results(a_in, a_out, self.meters.loudness.target, self.ceiling)

    def _analysis_failed(self, msg: str) -> None:
        self.analysis.set_error(msg)

    def _update_meters(self, snaps: list[dict]) -> None:
        src = self.meters.source
        for snap in snaps:
            self._play_clock += self.engine.block_size / max(self.engine.sample_rate, 1.0)
            for key in ("in", "out"):
                self.meters.loudness.push_history(key, self._play_clock, snap[key]["short_term"])
            self.stereo.push_gonio(*snap[src]["gonio"])
        last = snaps[-1]
        m = last[src]
        self.meters.loudness.update_values(m, src, self.ceiling)
        self.meters.correlation.update_values(m)
        self.meters.gr.update_values(last["gr"])
        self.stereo.update_values(m)
        grid = self.engine.meters_out.spectrum.grid
        self.spectrum.update_values(grid, last["in"]["spectrum"], last["out"]["spectrum"])
        ref = self._reference_on_grid(grid, last["out"]["spectrum"])
        if ref is not None:
            self.spectrum.set_reference(grid, ref)
        self._update_ab_label(last["ab"])

    # ---- export --------------------------------------------------------------
    def export_dialog(self) -> None:
        if self.audio is None:
            QMessageBox.information(self, "Export", "Open a file first.")
            return
        dlg = ExportDialog(self)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        bits, dither = dlg.values()
        default = str(self.audio.path.with_name(self.audio.path.stem + "_master.wav")) if self.audio.path else ""
        path, _ = QFileDialog.getSaveFileName(self, "Export master", default, "WAV (*.wav)")
        if path:
            self.export_to(path, bits, dither)

    def export_to(self, path: str, bits: str = "24", dither: bool = True) -> QThread:
        """Render the current chain to ``path`` on a background thread."""
        assert self.audio is not None
        worker = ExportWorker(self.engine.snapshot_chain(), self.audio, path, bits, dither)
        thread = QThread(self)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        self._progress = QProgressDialog("Rendering…", "Cancel", 0, 100, self)
        self._progress.setWindowModality(Qt.WindowModality.WindowModal)
        self._progress.canceled.connect(lambda: setattr(worker, "cancelled", True))
        worker.progress.connect(self._progress.setValue)
        # Bound methods of GUI-thread QObjects => queued connections (run on the GUI thread).
        worker.finished.connect(self._export_finished)
        worker.failed.connect(self._export_failed)
        thread.finished.connect(worker.deleteLater)
        self._export_worker = worker  # keep a reference
        self._export_thread = thread
        thread.start()
        return thread

    def _export_finished(self, path: str) -> None:
        self._export_done(f"Exported {path}")

    def _export_failed(self, msg: str) -> None:
        self._export_done(msg)

    def _export_done(self, msg: str) -> None:
        self._progress.close()
        if self._export_thread is not None:
            self._export_thread.quit()
        self.statusBar().showMessage(msg, 8000)

    # ---- periodic GUI update ----------------------------------------------
    def _tick(self) -> None:
        snaps = self.engine.meters.drain()
        if snaps:
            self._update_meters(snaps)
        n = self.engine.duration_samples
        if n:
            pos = self.engine.position
            if not self.pos_slider.isSliderDown():
                self.pos_slider.setValue(int(1000 * pos / n))
            sr = self.engine.sample_rate
            self.time_label.setText(f"{fmt_time(pos / sr)} / {fmt_time(n / sr)}")
        self.act_play.setText("Pause" if self.engine.playing else "Play")

    def showEvent(self, e) -> None:
        super().showEvent(e)
        if not self._docks_sized:  # only effective once the window has a size
            self._docks_sized = True
            self.resizeDocks([self._dock], [500], Qt.Orientation.Horizontal)

    def closeEvent(self, e) -> None:
        self._timer.stop()
        self.engine.stop_engine()
        for th in (self._export_thread, self._analysis_thread, *self._threads):
            if th is not None:
                th.quit()
                th.wait(5000)
        super().closeEvent(e)
