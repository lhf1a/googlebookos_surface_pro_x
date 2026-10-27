#!/usr/bin/env python3
"""Experimental SQ2 stop-pop fix: mute PA before DAPM/SDW teardown.

The 6.3 core calls machine hw_free before the normal close-time digital mute.
Keep the existing rate, gains, compander ports, and power-up sequence intact.
"""
from pathlib import Path
import sys

k = Path(sys.argv[1])
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
