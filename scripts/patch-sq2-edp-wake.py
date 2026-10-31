#!/usr/bin/env python3
"""Candidate: wake a previously discovered SQ2 eDP sink before rereading caps.

Only the pinned isolated V6 source is accepted. No live device operations.
V7 failed the 65Hz hardware test: the early D0 write also timed out. Kept as
an isolated experiment; do not add it to default builds or call it a fix.
"""
import hashlib
from pathlib import Path
import sys

EXPECTED = '7d87f7945c757b7f3e3eadbd86055a91765ebe4ab861d071bd49793dd691c706'
HELPER = '''static int sq2_edp_wake_before_caps(struct dp_panel *p,
				    struct dp_panel_private *priv)
{
	int attempt;
	ssize_t ret = -EIO;

	/* Cold discovery still reads the real revision first. Never guess caps. */
	if (!p->connector || p->connector->connector_type != DRM_MODE_CONNECTOR_eDP ||
	    !of_machine_is_compatible("microsoft,surface-pro-x") ||
	    p->link_info.revision < 0x11)
		return 0;

	/* A DPCD read itself probes REV and can fail while the receiver is in D3.
	 * Send the standard D0 request directly, before any capability read.
	 * Bound retries and propagate failure rather than using stale caps.
	 */
	for (attempt = 0; attempt < 3; attempt++) {
		ret = drm_dp_dpcd_writeb(priv->aux, DP_SET_POWER, DP_SET_POWER_D0);
		usleep_range(1000, 2000);
		if (ret == 1)
			break;
	}
	dev_info(priv->dev, "SQ2_EDP_WAKE attempts=%d rc=%zd\\n",
		 attempt < 3 ? attempt + 1 : 3, ret);
	return ret == 1 ? 0 : (ret < 0 ? ret : -EIO);
}

'''


def patch(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('Expected unmodified isolated cached-only V6 dp_panel.c')
    text = data.decode()
    anchor = 'static int dp_panel_read_dpcd(struct dp_panel *dp_panel)'
    assert text.count(anchor) == 1
    text = text.replace(anchor, HELPER + anchor)
    anchor = '\tlink_info = &dp_panel->link_info;\n\n\trlen = drm_dp_dpcd_read'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '\tlink_info = &dp_panel->link_info;\n\n'
                        '\trc = sq2_edp_wake_before_caps(dp_panel, panel);\n'
                        '\tif (rc)\n\t\treturn rc;\n\n'
                        '\trlen = drm_dp_dpcd_read')
    return text.encode()


if __name__ == '__main__':
    path = Path(sys.argv[1]) / 'drivers/gpu/drm/msm/dp/dp_panel.c'
    result = patch(path.read_bytes())
    path.write_bytes(result)
    print(hashlib.sha256(result).hexdigest())
