/* Private single-threaded probe only; reuse the audited transfer ABI tracer. */
#include "quest2-readback-trace.h"
void sq2_upload_trace_start(void) { quest2_install_readback_trace(8); }
void sq2_upload_trace_end(void) { quest2_report_readback_trace(); }
