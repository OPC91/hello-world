"""Phase 1 main window: load, transport, chain editing and export.

All chain mutations go through ``PlaybackEngine.post`` (thread-safe command
queue); the window only *reads* processor state to populate controls. Meter
snapshots are polled from the engine's SnapshotQueue on a ~30 fps timer.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from PySide6.QtCore import QObject, Qt, QThread, QTimer, Signal
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (
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
    QVBoxLayout,
    QWidget,
)

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.analysis import FileAnalysis, analyze
from mastermind.engine.commands import Reorder, SetBypass, SetParam
from mastermind.engine.offline import render, render_to_file
from mastermind.engine.realtime import PlaybackEngine
from mastermind.gui.chain_view import ChainListWidget
from mastermind.gui.meters.analysis_panel import AnalysisPanel
from mastermind.gui.meters.meter_panel import MeterPanel
from mastermind.gui.meters.spectrum_panel import SpectrumPanel
from mastermind.gui.meters.stereo_panel import StereoPanel
from mastermind.gui.param_panel import ParamPanel
from mastermind.io.audio_file import SUPPORTED_EXTENSIONS, AudioData, load_audio
from mastermind.io.export import BIT_DEPTHS

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
    def __init__(self, use_device: bool = True):
        super().__init__()
        self.setWindowTitle("MasterMind")
        self.resize(1400, 860)
        self.setAcceptDrops(True)
        self.engine = PlaybackEngine(use_device=use_device)
        self.audio: AudioData | None = None
        self._export_thread: QThread | None = None

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

        split = QSplitter()
        split.addWidget(self.chain_list)
        split.addWidget(self.tabs)
        split.setSizes([240, 760])

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
        self.spectrum.update_values(
            self.engine.meters_out.spectrum.grid, last["in"]["spectrum"], last["out"]["spectrum"]
        )

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
        for th in (self._export_thread, self._analysis_thread):
            if th is not None:
                th.quit()
                th.wait(5000)
        super().closeEvent(e)
