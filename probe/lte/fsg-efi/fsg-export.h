#ifndef SPX_FSG_EXPORT_H
#define SPX_FSG_EXPORT_H
#define SPX_FSG_BYTES (2u*1024u*1024u)
struct spx_fsg_report {
 struct spx_gpt_report gpt;
 EFI_STATUS status,allocation_status,call_status,free_status;
 UINT64 last_lba,results[4],returned_size;
 UINT32 reads,valid,copied_blocks,bytes;
 UINT8 data[SPX_FSG_BYTES];
};
#endif
