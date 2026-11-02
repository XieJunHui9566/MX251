/* Audit-only semantic observations.  GPL-2.0-or-later, like the backend. */
#ifndef MCS251_RETURN_AUDIT_H
#define MCS251_RETURN_AUDIT_H
#include "common.h"

void mcs251ReturnAuditInit (const char *path, const char *invocation_id);
void mcs251ReturnAuditObserveIR (ebbIndex *ebbi);
void mcs251ReturnAuditScanSymbolsBeforeGlue (void);
void mcs251ReturnAuditFinishAfterGlue (bool success);

#endif
