#!/usr/bin/env python3
"""Historical MSM get_modes experiment; NOT the current Pro-X boot solution.

Actual reboot showed built-in panel-edp bypasses this eDP mode callback. Use
build-sq2-edp-edid65.py plus a connector-scoped firmware override instead.

Original boot image/module remains the 60-Hz fallback. No panel firmware or
EDID bytes are written. Android boot at65 still needs device verification.
"""
from pathlib import Path
import hashlib,sys
p=Path(sys.argv[1])/'drivers/gpu/drm/msm/dp/dp_panel.c'
b=p.read_bytes()
assert hashlib.sha256(b).hexdigest()=='7d87f7945c757b7f3e3eadbd86055a91765ebe4ab861d071bd49793dd691c706'
s=b.decode()
start=s.index('int dp_panel_get_modes(struct dp_panel *dp_panel,')
end=s.index('static u8 dp_panel_get_edid_checksum(',start)
new='''/* SQ2_EDP_NATIVE65_V1: isolated default65 module, not a global override. */
static bool sq2_edp_set_native65(struct drm_display_mode *m)
{
	if (m->clock != 355500 || m->hdisplay != 2880 ||
	    m->hsync_start != 2928 || m->hsync_end != 2960 || m->htotal != 3000 ||
	    m->vdisplay != 1920 || m->vsync_start != 1941 ||
	    m->vsync_end != 1946 || m->vtotal != 1975 || m->flags != 0x0a)
		return false;
	m->clock = 385125;
	m->type |= DRM_MODE_TYPE_PREFERRED;
	/* DRM probe helper recomputes crtc timings after get_modes(). */
	return true;
}

int dp_panel_get_modes(struct dp_panel *dp_panel,
	struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	const u8 *edid;
	int count;
	if (!dp_panel) {
		DRM_ERROR("invalid input\\n");
		return -EINVAL;
	}
	if (!dp_panel->edid)
		return 0;
	count = dp_panel_update_modes(connector, dp_panel->edid);
	edid = (const u8 *)dp_panel->edid;
	if (count <= 0 || connector->connector_type != DRM_MODE_CONNECTOR_eDP ||
	    !of_machine_is_compatible("microsoft,surface-pro-x") ||
	    dp_panel->link_info.rate < 540000 || dp_panel->link_info.num_lanes != 4 ||
	    edid[8] != 0x30 || edid[9] != 0xe4 || edid[10] != 0x55 || edid[11] != 0x05 ||
	    memcmp(edid + 113, "LP129WT112684", 13))
		return count;
	list_for_each_entry(mode, &connector->probed_modes, head)
		sq2_edp_set_native65(mode);
	return count;
}

'''
result=(s[:start]+new+s[end:]).encode()
p.write_bytes(result)
assert p.read_bytes()==result
print('Native65 scoped preference: clock385125, original porches/polarities; SHA256',hashlib.sha256(result).hexdigest())
