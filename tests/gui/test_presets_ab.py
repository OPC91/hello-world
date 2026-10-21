import pytest
from PySide6.QtCore import Qt

pytestmark = pytest.mark.gui


@pytest.fixture
def window(qtbot, tmp_path):
    from mastermind.gui.main_window import MainWindow
    from mastermind.presets.store import PresetLibrary

    w = MainWindow(use_device=False, library=PresetLibrary(tmp_path))
    qtbot.addWidget(w)
    yield w
    w.close()


def test_browser_search_filter_favourite(window):
    b = window.browser
    assert b.list.count() == 35
    b.search.setText("techno")
    assert [b.list.item(i).data(Qt.ItemDataRole.UserRole) for i in range(b.list.count())] == ["electronic/techno"]
    b.list.setCurrentRow(0)
    b.btn_fav.click()
    assert window.library.is_favorite("electronic/techno")
    b.search.clear()
    b.category.setCurrentText("Favorites")
    assert b.list.count() == 1
    b.category.setCurrentText("Metal")
    assert b.list.count() == 4


def test_apply_preset_sets_chain_target_and_auto_gain(window, qtbot, wav_file):
    window.open_file(wav_file)
    p = window.library.get("electronic/techno")
    th = window.apply_preset(p, auto_gain=True)
    assert window.engine.chain.find("eq").get_param("b4_gain") == p.build_chain().find("eq").get_param("b4_gain")
    assert window.meters.loudness.target == -8.0
    qtbot.waitUntil(lambda: window.last_fit is not None, timeout=60000)
    th.wait(5000)
    qtbot.waitUntil(
        lambda: window.engine.chain.find("limiter").get_param("drive_db") == window.last_fit.drive_db, timeout=3000
    )
    assert window.last_fit.achieved_lufs == pytest.approx(-8.0, abs=0.5)


def test_save_current_as_user_preset(window):
    window.apply_preset(window.library.get("rock/punk"), auto_gain=False)
    window.engine.post(__import__("mastermind.engine.commands", fromlist=["SetParam"]).SetParam("glue", "ratio", 3.3))
    saved = window.save_current_as("My Punk", "Rock", "tweaked")
    assert saved.id == "user/my-punk"
    assert saved.target.lufs == window.library.get("rock/punk").target.lufs
    assert window.browser.selected_id() == "user/my-punk"
    assert window.library.get("user/my-punk").build_chain().find("glue").get_param("ratio") == 3.3


def test_ab_toolbar(window, qtbot, wav_file):
    window.open_file(wav_file)
    window.apply_preset(window.library.get("pop/k-pop"), auto_gain=False)
    window.btn_b.click()
    assert window.engine.active == "B" and window.engine.ab_enabled
    assert window.engine.chain.to_dict() == window.engine.chains["A"].to_dict()
    window.apply_preset(window.library.get("other/jazz"), auto_gain=False)
    assert window.engine.chains["B"].find("eq").get_param("mode") == "linear"
    assert window.engine.chains["A"].find("eq").get_param("mode") == "minimum"
    window.btn_a.click()
    assert window.engine.active == "A"
    window.copy_ab("B", "A")
    assert window.engine.chains["A"].to_dict() == window.engine.chains["B"].to_dict()
    window.btn_orig.setChecked(True)
    assert window.engine.monitor_original
    window.match_box.setChecked(False)
    assert not window.engine.loudness_match
    window.toggle_play()
    qtbot.waitUntil(lambda: window.ab_label.text() != "", timeout=8000)


def test_preset_reference_curve_on_spectrum(window, qtbot, wav_file):
    window.open_file(wav_file)
    window.apply_preset(window.library.get("hip-hop/trap"), auto_gain=False)
    window.toggle_play()

    def has_reference() -> bool:
        x = window.spectrum.ref.getData()[0]
        return x is not None and len(x) > 0

    qtbot.waitUntil(has_reference, timeout=8000)
