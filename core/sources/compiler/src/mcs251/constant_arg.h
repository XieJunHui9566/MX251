/* GPL-2.0-or-later. Explicit whole-image constant parameter contract. */
#ifndef MCS251_CONSTANT_ARG_H
#define MCS251_CONSTANT_ARG_H
#include "common.h"
operand *mcs251ConstantParameterValue (symbol *function, value *formal, unsigned ordinal);
bool mcs251ConstantParameterApplied (symbol *function, unsigned *bits);
bool mcs251ConstantParameterAppliedAt(symbol *,unsigned,unsigned *);
/* Shared container-v1 parser copied byte-identically from its frozen owner. */
extern char *mcs251ParameterDomainFile;
extern char *mcs251ScalarParameterFile;
void mcs251LoadParameterDomains(void);
unsigned mcs251ParameterDomainValues(symbol *,value *,unsigned,unsigned values[4]);
#endif
