/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有界存储保持可整体复制；环境选项只控制本次编译允许导入的大小。
 * 只记录拒绝原因，不放宽类型、访存、调用约定或候选成本的任何条件。
 */
static bool
wide_reject(wide_function *f,const char *reason,int line)
{
  /* 容量检查可在操作数导入中失败；保留它，不被外层语义诊断覆盖。 */
  if(!f->rejectionReason) {f->rejectionReason=reason;f->rejectionLine=line;}
  return false;
}

static bool
wide_capacity_profile(wide_function *f)
{
  const char *values=getenv("MX251_WIDE_VALUE_LIMIT");
  const char *nodes=getenv("MX251_WIDE_NODE_LIMIT");
  f->valueLimit=128;f->nodeLimit=512;
  if(values) {
    if(!strcmp(values,"128"))f->valueLimit=128;
    else if(!strcmp(values,"256"))f->valueLimit=256;
    else if(!strcmp(values,"512"))f->valueLimit=512;
    else return wide_reject(f,"invalid-MX251_WIDE_VALUE_LIMIT",__LINE__);
  }
  if(nodes) {
    if(!strcmp(nodes,"512"))f->nodeLimit=512;
    else if(!strcmp(nodes,"1024"))f->nodeLimit=1024;
    else if(!strcmp(nodes,"2048"))f->nodeLimit=2048;
    else return wide_reject(f,"invalid-MX251_WIDE_NODE_LIMIT",__LINE__);
  }
  wassert(f->valueLimit<=WV_MAX && f->nodeLimit<=WN_MAX);
  return true;
}

static bool
wide_capacity_room(wide_function *f,int values,int nodes,int line)
{
  wassert(values>=0 && nodes>=0 && f->nv>=0 && f->nn>=0);
  wassert(f->nv<=f->valueLimit && f->nn<=f->nodeLimit);
  if(values>f->valueLimit-f->nv)return wide_reject(f,"value-capacity",line);
  if(nodes>f->nodeLimit-f->nn)return wide_reject(f,"node-capacity",line);
  return true;
}

static bool
wide_import_reject(wide_function *f,iCode *ic,int line)
{
  const char *reason="semantic-import-contract";
  if(ic) {
    switch(ic->op) {
    case INLINEASM: reason="unsupported-inline-asm";break;
    case DUMMY_READ_VOLATILE: reason="unsupported-volatile-dummy-read";break;
    case ADDRESS_OF: reason="address-space-or-object-contract";break;
    case GET_VALUE_AT_ADDRESS: reason="memory-access-contract";break;
    case CALL: reason="call-abi-or-argument-contract";break;
    case IPUSH: case SEND: reason="call-parameter-sequence-contract";break;
    case RECEIVE: case RETURN: reason="entry-or-return-contract";break;
    case '=': case CAST: reason="assignment-cast-or-memory-contract";break;
    case '+': case '-': case '*': case '^': case '|': case BITWISEAND:
    case LEFT_OP: case RIGHT_OP:
    case '<': case '>': case LE_OP: case GE_OP: case EQ_OP: case NE_OP:
      reason="arithmetic-width-type-or-operand-contract";break;
    default: break;
    }
  }
  return wide_reject(f,reason,line);
}
