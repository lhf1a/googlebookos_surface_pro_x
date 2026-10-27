#!/usr/bin/env python3
"""Backport the closed-serdev QCA shutdown guard to the pinned SQ2 6.3 tree.

Upstream 4e93c65f87825e1e012bce56615320aeb123815d / CVE-2026-98294:
https://git.kernel.org/stable/c/4e93c65f87825e1e012bce56615320aeb123815d
The local trace is tty_set_termios -> qca_power_shutdown -> qca_serdev_remove.
Keep regulator shutdown unconditional; only avoid I/O on a closed UART.
"""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
p=Path('/root/.cache/sq2-probe-build/kernel/drivers/bluetooth/hci_qca.c')
text=p.read_text()
old='''	if (qca_is_wcn399x(soc_type)) {
		host_set_baudrate(hu, 2400);
		qca_send_power_pulse(hu, false);
		qca_regulator_disable(qcadev);'''
new='''	if (qca_is_wcn399x(soc_type)) {
		/* Backport 4e93c65f8782: failed setup may have closed serdev. */
		if (test_bit(HCI_UART_PROTO_READY, &hu->flags)) {
			host_set_baudrate(hu, 2400);
			qca_send_power_pulse(hu, false);
		}
		qca_regulator_disable(qcadev);'''
if new not in text:
    assert text.count(old)==1
    p.write_text(text.replace(old,new))
text=p.read_text()
old='''static int qca_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;
'''
new=old+'''
	/* SQ2 WCN3998: reopening emits ff 02 92 01 (baud-change ack).
	 * It is not the three-byte TLV download response. Discard only this
	 * exact asynchronous event during setup, preserving all real TLV
	 * responses and runtime vendor events. Captured on physical SQ2.
	 */
	if (qca_soc_type(hu) == QCA_WCN3998 &&
	    test_bit(HCI_INIT, &hdev->flags) && skb->len == 4 &&
	    skb->data[0] == HCI_EV_VENDOR && skb->data[1] == 2 &&
	    skb->data[2] == EDL_SET_BAUDRATE_RSP_EVT && skb->data[3] == 1) {
		bt_dev_info(hdev, "SQ2: consumed WCN3998 asynchronous baudrate ack");
		kfree_skb(skb);
		return 0;
	}
'''
if new not in text:
    assert text.count(old)==1
    p.write_text(text.replace(old,new))
tlv=p.with_name('btqca.c')
text=tlv.read_text()
old='bt_dev_err(hdev, "QCA TLV response size mismatch");'
new='bt_dev_err(hdev, "QCA TLV response size mismatch: got %u expected %u data %*ph",\n\t\t\t   skb->len, rlen, min_t(unsigned int, skb->len, 16), skb->data);'
if new not in text:
    assert text.count(old)==1
    tlv.write_text(text.replace(old,new))
report={'upstream':'4e93c65f87825e1e012bce56615320aeb123815d','file':str(p),
        'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
        'tlv_diagnostics_sha256':hashlib.sha256(tlv.read_bytes()).hexdigest(),
        'sq2_reopen_fix':'consume exact ff 02 92 01 baud acknowledgement during WCN3998 setup',
        'hardware_retest':False}
(root/'out/connectivity/bluetooth-kernel-patch.json').write_text(json.dumps(report,indent=2)+'\n')
print('Applied QCA closed-UART shutdown guard; controller reopen remains separate work')
