"""Offscreen GUI smoke test (QT_QPA_PLATFORM=offscreen is set in conftest)."""

import pytest
import soundfile as sf
from PySide6.QtCore import Qt

pytestmark = pytest.mark.gui


@pytest.fixture
def window(qtbot):
    from mastermind.gui.main_window import MainWindow

    w = MainWindow(use_device=False)
    qtbot.addWidget(w)
    yield w
    w.close()


def test_load_play_edit_export(window, qtbot, wav_file, tmp_path):
    window.open_file(wav_file)
    assert window.engine.duration_samples > 0
    window.toggle_play()
    qtbot.waitUntil(lambda: window.engine.position > 0, timeout=5000)

    # Edit a parameter through the auto-generated panel.
    row = [window.chain_list.item(i).data(Qt.ItemDataRole.UserRole) for i in range(window.chain_list.count())]
    window.chain_list.setCurrentRow(row.index("limiter"))
    window.params.controls["drive_db"].spin.setValue(6.0)
    qtbot.waitUntil(lambda: window.engine.chain.find("limiter").get_param("drive_db") == 6.0, timeout=2000)

    # Un-bypass saturation via its checkbox.
    window.chain_list.item(row.index("saturation")).setCheckState(Qt.CheckState.Checked)
    qtbot.waitUntil(
        lambda: not window.engine.chain.slots[window.engine.chain.index_of("saturation")].bypass, timeout=2000
    )

    out = tmp_path / "gui.wav"
    thread = window.export_to(str(out), "24", True)
    qtbot.waitUntil(lambda: thread.isFinished(), timeout=30000)
    qtbot.waitUntil(lambda: "Exported" in window.statusBar().currentMessage(), timeout=2000)
    assert sf.info(str(out)).subtype == "PCM_24"


def test_param_panel_covers_every_module(window):
    chain = window.engine.chain
    for slot in chain.slots:
        window.params.show_processor(slot.processor)
        assert set(window.params.controls) == {s.id for s in slot.processor.param_specs()}
