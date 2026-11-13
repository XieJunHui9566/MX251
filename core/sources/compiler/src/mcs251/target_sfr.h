/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_TARGET_SFR_H
#define MCS251_TARGET_SFR_H
/* Only the SPH mapping differs in this contract. Other reserved core
 * addresses retain their existing classification. Selected by the driver. */
extern unsigned mcs251SphAddress;
static inline int mcs251StackSfrAddress(unsigned address)
{ return address==0x81 || address==mcs251SphAddress; }
#endif
