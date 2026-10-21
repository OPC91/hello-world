import json

import soundfile as sf

from mastermind.cli import main
from mastermind.dsp.primitives.truepeak import true_peak_dbtp
from mastermind.io.audio_file import load_audio


def test_master_end_to_end(wav_file, tmp_path, capsys):
    out = tmp_path / "master.wav"
    assert (
        main(["master", wav_file, "-o", str(out), "--gain", "8", "--ceiling", "-1", "--bits", "16", "--json", "-q"])
        == 0
    )
    report = json.loads(capsys.readouterr().out)
    assert report["output"]["true_peak_dbtp"] <= -1.0
    assert report["output"]["integrated_lufs"] > report["input"]["integrated_lufs"]
    info = sf.info(str(out))
    assert info.subtype == "PCM_16"
    audio = load_audio(out)
    assert audio.n_samples == load_audio(wav_file).n_samples
    assert true_peak_dbtp(audio.samples) <= -1.0 + 0.01  # 16-bit requantisation + dither


def test_chain_template_round_trip(wav_file, tmp_path, capsys):
    assert main(["chain-template"]) == 0
    tpl = json.loads(capsys.readouterr().out)
    for slot in tpl["slots"]:
        if slot["type"] == "glue":
            slot["params"]["threshold"] = -20.0
    path = tmp_path / "chain.json"
    path.write_text(json.dumps(tpl))
    out = tmp_path / "o.wav"
    assert main(["master", wav_file, "-o", str(out), "--chain", str(path), "--bits", "32f", "-q"]) == 0
    text = capsys.readouterr().out
    assert "Integrated loudness" in text and out.exists()


def test_analyze(wav_file, capsys):
    assert main(["analyze", wav_file, "--json"]) == 0
    rep = json.loads(capsys.readouterr().out)
    assert -40 < rep["integrated_lufs"] < 0
    assert 0.5 < rep["correlation"] <= 1.0
