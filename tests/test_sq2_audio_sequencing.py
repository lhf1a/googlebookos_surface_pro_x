"""Apply the existing stop-pop patch to real kernel sources and check ordering."""
from pathlib import Path
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    names = ('sound/soc/qcom/sdm845.c', 'sound/soc/codecs/wsa881x.c')
    for name in names:
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((Path(sys.argv[1])/name).read_bytes())
    command = [sys.executable, str(R/'scripts/patch-sq2-audio-sequencing.py'), str(root)]
    original_codec = (root/names[1]).read_bytes()
    subprocess.run(command + ['--machine-only'], check=True)
    machine_only = (root/names[0]).read_bytes()
    subprocess.run(command + ['--machine-only'], check=True)
    assert (root/names[0]).read_bytes() == machine_only
    assert (root/names[1]).read_bytes() == original_codec
    subprocess.run(command, check=True)
    first = [(root/name).read_bytes() for name in names]
    subprocess.run(command, check=True)
    assert first == [(root/name).read_bytes() for name in names]
    machine, codec = [data.decode() for data in first]
    assert first[0] == machine_only
    refused = subprocess.run(command + ['--machine-only'], capture_output=True, text=True)
    assert refused.returncode == 2 and 'original WSA881x' in refused.stderr
    assert machine.count('sq2_mute_sdw_speakers(substream);\n\t\tsdw_disable_stream(sruntime);') == 2
    assert '"microsoft,surface-pro-x-sndcard"' in machine
    assert 'strcmp(dai->component->driver->name, "WSA881x")' in machine
    event = codec[codec.index('static int wsa881x_spkr_pa_event('):]
    event = event[:event.index('static const struct snd_soc_dapm_widget')]
    pre = event[event.index('case SND_SOC_DAPM_PRE_PMD:'):event.index('case SND_SOC_DAPM_POST_PMD:')]
    assert 'WSA881X_SPKR_DRV_EN' in pre and '0x80, 0x00' in pre
    assert 'usleep_range(2000, 2100)' in pre
    assert 'SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |' in codec
print('PASS: machine-only ordering/idempotence, unchanged codec and failed combined variant rejected')
