/* SPDX-License-Identifier: GPL-2.0-only */
/* Default timeout emits one last-poll snapshot without extending the wait.
 * Explicit boot_watch retains its extended diagnostic window. Only the existing
 * CTRL_STATUS value and CPU-owned coherent queues/SFR are read; no new MMIO.
 */
static bool sq2_boot_watch;
module_param(sq2_boot_watch, bool, 0400);
static void sq2_boot_snapshot(struct venus_hfi_device *hdev, u32 status,
                              unsigned int count)
{
 struct hfi_queue_header *cmd, *msg, *dbg;
 u32 *sfr;
 if (!IS_SQ2_HFI(hdev->core)) return;
 /* The unmodified default boot loop ends after poll99 if status stays zero. */
 if (!sq2_boot_watch && (count != 99 || status)) return;
 cmd = hdev->queues[0].qhdr;
 msg = hdev->queues[1].qhdr;
 dbg = hdev->queues[2].qhdr;
 sfr = hdev->sfr.kva;
 if (!cmd || !msg || !dbg || !sfr || hdev->sfr.size < 20) return;
 dma_rmb();
 dev_info(hdev->core->dev,
  "SQ2_BOOT_WATCH enabled=%u poll=%u status=%08x uc=%pad cmd=%u,%u rx=%u msg=%u dbg=%u sfr=%08x,%08x,%08x,%08x\n",
  sq2_boot_watch, count, status, &hdev->ifaceq_table.da, READ_ONCE(cmd->read_idx),
  READ_ONCE(cmd->write_idx), READ_ONCE(cmd->rx_req), READ_ONCE(msg->write_idx),
  READ_ONCE(dbg->write_idx), READ_ONCE(sfr[1]), READ_ONCE(sfr[2]),
  READ_ONCE(sfr[3]), READ_ONCE(sfr[4]));
}
