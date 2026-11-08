/* Existing Venus event dispatcher before the late-session guard. */
static void hfi_event_notify(struct venus_core *core, struct venus_inst *inst,
			     void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;

	if (!packet)
		return;

	switch (pkt->event_id) {
	case HFI_EVENT_SYS_ERROR:
		event_sys_error(core, EVT_SYS_ERROR, pkt);
		break;
	case HFI_EVENT_SESSION_ERROR:
		event_session_error(core, inst, pkt);
		break;
	case HFI_EVENT_SESSION_SEQUENCE_CHANGED:
		event_seq_changed(core, inst, pkt);
		break;
	case HFI_EVENT_RELEASE_BUFFER_REFERENCE:
		event_release_buffer_ref(core, inst, pkt);
		break;
	case HFI_EVENT_SESSION_PROPERTY_CHANGED:
		break;
	default:
		break;
	}
}

#if 0
dev_warn(dev, "no valid instance(pkt session_id:%x, pkt:%x)\n",
dev_err(dev, "got invalid session id:%x\n",
#endif
