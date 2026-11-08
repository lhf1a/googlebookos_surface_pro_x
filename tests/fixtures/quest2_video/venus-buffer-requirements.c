static unsigned int
session_get_prop_buf_req(struct hfi_msg_session_property_info_pkt *pkt,
			 struct hfi_buffer_requirements *bufreq)
{
	struct hfi_buffer_requirements *buf_req;
	u32 req_bytes;
	unsigned int idx = 0;

	req_bytes = pkt->shdr.hdr.size - sizeof(*pkt);

	if (!req_bytes || req_bytes % sizeof(*buf_req) || !pkt->data[0])
		/* bad packet */
		return HFI_ERR_SESSION_INVALID_PARAMETER;

	buf_req = (struct hfi_buffer_requirements *)&pkt->data[0];
	if (!buf_req)
		return HFI_ERR_SESSION_INVALID_PARAMETER;

	while (req_bytes) {
		memcpy(&bufreq[idx], buf_req, sizeof(*bufreq));
		idx++;

		if (idx > HFI_BUFFER_TYPE_MAX)
			return HFI_ERR_SESSION_INVALID_PARAMETER;

		req_bytes -= sizeof(struct hfi_buffer_requirements);
		buf_req++;
	}

	return HFI_ERR_NONE;
}
