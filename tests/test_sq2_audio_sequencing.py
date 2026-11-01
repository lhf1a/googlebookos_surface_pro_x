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
    subprocess.run(command, check=True)
    first = [(root/name).read_bytes() for name in names]
    subprocess.run(command, check=True)
    assert first == [(root/name).read_bytes() for name in names]
    machine, codec = [data.decode() for data in first]
    assert machine.count('sq2_mute_sdw_speakers(substream);\n\t\tsdw_disable_stream(sruntime);') == 2
    assert '"microsoft,surface-pro-x-sndcard"' in machine
    assert 'strcmp(dai->component->driver->name, "WSA881x")' in machine
    event = codec[codec.index('static int wsa881x_spkr_pa_event('):]
    event = event[:event.index('static const struct snd_soc_dapm_widget')]
    pre = event[event.index('case SND_SOC_DAPM_PRE_PMD:'):event.index('case SND_SOC_DAPM_POST_PMD:')]
    assert 'WSA881X_SPKR_DRV_EN' in pre and '0x80, 0x00' in pre
    assert 'usleep_range(2000, 2100)' in pre
    assert 'SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |' in codec
    build = (R/'scripts/build-sq2-el2-android-kernel.sh').read_text()
    # This patch alone silenced the EL2 port; retain only as a diagnostic.
    assert 'patch-sq2-audio-sequencing.py' not in build
print('PASS: diagnostic patch ordering/idempotence; failed EL2 candidate excluded from default')
