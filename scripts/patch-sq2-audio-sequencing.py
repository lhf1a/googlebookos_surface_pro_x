#!/usr/bin/env python3
"""SQ2 stop-pop fix: mute PA before SoundWire teardown.

The 6.3 core calls machine hw_free before the normal close-time digital mute.
EL2 uses --machine-only: the legacy codec PRE_PMD change silenced this port.
Keep the existing rate, gains, compander ports, and power-up sequence intact.
"""
from pathlib import Path
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kernel', type=Path)
parser.add_argument('--machine-only', action='store_true', help='EL2: retain the original WSA881x driver')
args = parser.parse_args()
k = args.kernel
if args.machine_only and '/* SQ2 stop-pop: mute before RDAC' in (k/'sound/soc/codecs/wsa881x.c').read_text():
    parser.error('EL2 requires the original WSA881x source; restore it before applying --machine-only')
p = k / 'sound/soc/qcom/sdm845.c'
s = p.read_text()
marker = 'static void sq2_mute_sdw_speakers('
if marker not in s:
    anchor = 'static int sdm845_snd_prepare(struct snd_pcm_substream *substream)'
    assert s.count(anchor) == 1
    assert s.count('\t\tsdw_disable_stream(sruntime);') == 2
    helper = '''/* SQ2: PA must be quiet before the SoundWire stream loses its clock/data. */
static void sq2_mute_sdw_speakers(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *dai;
	bool muted = false;
	int i, ret;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK ||
	    !of_device_is_compatible(rtd->card->dev->of_node,
				     "microsoft,surface-pro-x-sndcard"))
		return;

	for_each_rtd_codec_dais(rtd, i, dai) {
		if (!dai->component->driver->name ||
		    strcmp(dai->component->driver->name, "WSA881x"))
			continue;
		ret = snd_soc_dai_digital_mute(dai, 1, substream->stream);
		if (ret < 0)
			dev_warn_ratelimited(rtd->dev, "SQ2 PA mute failed: %d\\n", ret);
		else
			muted = true;
	}
	if (muted)
		usleep_range(2000, 2100);
}

'''
    s = s.replace('#include <linux/module.h>', '#include <linux/module.h>\n#include <linux/delay.h>')
    s = s.replace(anchor, helper + anchor)
    s = s.replace('\t\tsdw_disable_stream(sruntime);',
                  '\t\tsq2_mute_sdw_speakers(substream);\n\t\tsdw_disable_stream(sruntime);')
    p.write_text(s)

if args.machine_only:
    print('SQ2 machine mute-before-SoundWire patch applied; WSA881x unchanged')
    raise SystemExit(0)

p = k / 'sound/soc/codecs/wsa881x.c'
s = p.read_text()
marker = '/* SQ2 stop-pop: mute before RDAC and clock supplies power down. */'
if marker not in s:
    start = s.index('static int wsa881x_spkr_pa_event(')
    end = s.index('static const struct snd_soc_dapm_widget', start)
    event = s[start:end]
    anchor = '\tcase SND_SOC_DAPM_POST_PMD:'
    assert event.count(anchor) == 1
    event = event.replace(anchor, '''	case SND_SOC_DAPM_PRE_PMD:
		/* SQ2 stop-pop: mute before RDAC and clock supplies power down. */
		snd_soc_component_update_bits(comp, WSA881X_SPKR_DRV_EN,
					      0x80, 0x00);
		usleep_range(2000, 2100);
		break;
''' + anchor)
    s = s[:start] + event + s[end:]
    anchor = 'SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),'
    assert s.count(anchor) == 1
    s = s.replace(anchor, 'SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |\n\t\t\t   SND_SOC_DAPM_POST_PMD),')
    p.write_text(s)
print('SQ2 audio mute-before-teardown patch applied')
