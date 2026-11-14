/* SPDX-License-Identifier: GPL-2.0-or-later
 * Replace small, nonescaping local tables with typed constant selections.
 * Every element has one constant initializer dominating every read. Derived
 * addresses may only feed the table's own accesses or further address values.
 */
typedef struct {int object,offset;wide_operand index;} wide_table_address;

static wide_node *
wide_table_definition(wide_function *f,wide_address_dominance *d,wide_operand o,int at)
{
  return wide_address_dominates(f,d,o.value,at)?&f->nodes[d->definition[o.value]]:NULL;
}

static bool
wide_table_constant(wide_function *f,wide_address_dominance *d,wide_operand o,int at,
                    wide_table_entry *entry,unsigned depth)
{
  if(depth>16 || o.floating)return false;
  if(o.value<0) {*entry=(wide_table_entry){.literal=o};return true;}
  wide_node *n=wide_table_definition(f,d,o,at);
  if(!n || n->byteAccess || n->orderedReadCount)return false;
  if(n->op==WM_ADDRESS && n->addressObject<0 && n->addressSymbol &&
     !n->addressSymbol->onStack && !n->addressOp) {
    *entry=(wide_table_entry){.literal={.value=-1,.width=4},
      .symbol=n->addressSymbol,.offset=n->addressOffset};return true;
  }
  if((n->op=='=' || n->op==CAST) && n->right.width==o.width &&
     wide_table_constant(f,d,n->right,d->definition[o.value],entry,depth+1)) {
    if(f->values[o.value].pointer)entry->literal.literal&=UINT32_C(0x00ffffff);
    return true;
  }
  return false;
}

/* Small nonnegative bounds suffice here; no new general range engine. */
static bool
wide_table_bound(wide_function *f,wide_address_dominance *d,wide_operand o,int at,
                 unsigned limit,unsigned depth)
{
  if(depth>16 || o.floating || o.codePointer)return false;
  if(o.value<0)return o.literal<limit;
  if(f->values[o.value].pointer)return false;
  wide_node *n=wide_table_definition(f,d,o,at);
  if(!n)return false;
  if((n->op=='=' || n->op==CAST) && !n->floating)
    return wide_table_bound(f,d,n->right,d->definition[o.value],limit,depth+1);
  if(n->op==BITWISEAND && !n->floating) {
    wide_operand mask=n->right.value<0?n->right:n->left;
    return mask.value<0 && mask.literal<limit;
  }
  return limit>=2 && (n->op=='!' || n->op==WM_BIT_CAST || n->op==EQ_OP ||
    n->op==NE_OP || n->op=='<' || n->op=='>' || n->op==LE_OP || n->op==GE_OP);
}

static bool
wide_table_index(wide_function *f,wide_address_dominance *d,wide_operand o,int at,
                 unsigned scale,unsigned count,wide_operand *index,unsigned depth)
{
  if(depth>16 || o.floating || o.codePointer || (o.width!=1 && o.width!=2 && o.width!=4))return false;
  if(o.value<0) {
    if(o.literal%scale || o.literal/scale>=count)return false;
    *index=(wide_operand){.value=-1,.width=1,.literal=o.literal/scale};return true;
  }
  wide_node *n=wide_table_definition(f,d,o,at);
  if(n && (n->op=='=' || n->op==CAST) && !n->floating &&
     wide_table_index(f,d,n->right,d->definition[o.value],scale,count,index,depth+1))return true;
  if(scale==1 && wide_table_bound(f,d,o,at,count,0)) {*index=o;return true;}
  if(!n || n->floating)return false;
  wide_operand input={.value=-1};
  if(n->op==WM_UMULW || n->op=='*') {
    wide_operand constant=n->right.value<0?n->right:n->left;
    if(constant.value>=0 || constant.literal!=scale)return false;
    input=n->right.value<0?n->left:n->right;
  } else if(n->op==LEFT_OP && n->right.value<0 && n->right.literal<4 &&
            (1u<<n->right.literal)==scale)input=n->left;
  else return false;
  return wide_table_index(f,d,input,d->definition[o.value],1,count,index,depth+1);
}

static void
wide_table_nop(wide_node *n)
{
  iCode *origin=n->origin;int a=n->succ[0],b=n->succ[1];
  *n=(wide_node){.op=WM_NOP,.origin=origin,.result=-1,.pairedResult=-1,
    .left.value=-1,.right.value=-1,.addressIndex.value=-1,.addressObject=-1,
    .label=-1,.target=-1,.succ={a,b}};
}

static unsigned
wide_constant_tables(wide_function *f)
{
  const char *enable=getenv("MX251_LOCAL_CONSTANT_TABLES");
  if(!f->no || (enable && !strcmp(enable,"0")))return 0;
  for(int i=0;i<f->nn;++i)if(f->nodes[i].op==WM_ORDERED_ASM)return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  int predecessors[WN_MAX]={0};
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];if(n->op==FUNCTION)d->entry=i;
    for(int v=0;v<f->nv;++v)if(wb_has(n->def,v)) {++d->definitions[v];d->definition[v]=i;}
    for(int k=0;k<2+n->jumpCount;++k) {
      int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(to>=0)++predecessors[to];
    }
  }
  wide_address_reachable(f,d->entry,-1,d->reachable);
  bool removed[WO_MAX]={0};unsigned changes=0;
  for(int object=0;object<f->no && f->constantTableCount<WO_MAX;++object) {
    symbol *s=f->objects[object].sym;
    if(!s->onStack || !IS_ARRAY(s->type) || isVolatile(s->type) || isAtomic(s->type))continue;
    sym_link *type=s->type->next;
    if(!wide_abi_type(type,currFunc->type) || IS_FLOAT(type))continue;
    int physical=getSize(type),width=wide_width(type),count=f->objects[object].size/physical;
    if(count<2 || count>WTABLE_ENTRIES || count*physical!=f->objects[object].size)continue;
    wide_table_address addresses[WV_MAX];
    for(int v=0;v<f->nv;++v)addresses[v]=(wide_table_address){.object=-1,.index.value=-1};
    bool progress;
    do {
      progress=false;
      for(int i=0;i<f->nn;++i) {
        wide_node *n=&f->nodes[i];int v=n->result,source=-1;
        if(v<0 || addresses[v].object>=0 || !wide_fixed_address_value(&f->values[v]) ||
           d->definitions[v]!=1 || !d->reachable[i])continue;
        wide_table_address p={.object=-1,.index={.value=-1,.width=1}};
        if(n->op==WM_ADDRESS && n->addressObject==object) {
          p.object=object;p.offset=n->addressOffset;
          if(n->addressOp && (n->addressOp!='+' || p.offset ||
             !wide_table_index(f,d,n->addressIndex,i,physical,count,&p.index,0)))continue;
        } else if((n->op=='=' || n->op==CAST) && n->right.width==4)source=n->right.value;
        else if(n->op=='+' && n->left.width==4)source=n->left.value;
        if(source>=0 && addresses[source].object==object && wide_address_dominates(f,d,source,i)) {
          p=addresses[source];
          if(n->op=='+') {
            if(n->right.value<0 && !n->right.floating) {
              int64_t delta;
              if(!wide_fixed_address_delta(n->right,&delta) || p.offset+delta<0 ||
                 p.offset+delta>=f->objects[object].size)continue;
              p.offset+=(int)delta;
            }
            else if(p.offset || p.index.value>=0 || p.index.literal ||
                    !wide_table_index(f,d,n->right,i,physical,count,&p.index,0))continue;
          }
        }
        if(p.object==object && p.offset>=0 && p.offset<f->objects[object].size) {
          addresses[v]=p;progress=true;
        }
      }
    } while(progress);
    wide_operand indices[WN_MAX];int stores[WTABLE_ENTRIES];bool access[WN_MAX]={0};
    wide_constant_table table={.count=count};unsigned loads=0;bool valid=true;
    for(int k=0;k<count;++k)stores[k]=-1;
    for(int i=0;i<f->nn && valid;++i) {
      wide_node *n=&f->nodes[i];if(n->op==WM_NOP)continue;
      bool load=n->op==WM_LOAD || n->op==WM_OBJECT_LOAD;
      bool store=n->op==WM_STORE || n->op==WM_OBJECT_STORE;
      wide_table_address p={.object=-1,.index={.value=-1,.width=1}};
      if((load || store) && n->addressObject==object) {p.object=object;p.offset=n->addressOffset;}
      else if((load || store) && n->left.value>=0 && addresses[n->left.value].object==object &&
              wide_address_dominates(f,d,n->left.value,i))p=addresses[n->left.value];
      if(p.object==object) {
        if(n->addressOp) {
          if(n->addressOp!='+' || p.offset || p.index.value>=0 || p.index.literal ||
             !wide_table_index(f,d,n->addressIndex,i,physical,count,&p.index,0)) {valid=false;break;}
        }
        if((n->addressObject<0 && n->addressOffset) || n->byteAccess || n->orderedReadCount || n->codeAccess || n->width!=width ||
           n->pointerAccess!=wide_pointer(type) || n->floating ||
           (p.index.value>=0 && p.offset) || p.offset%physical) {valid=false;break;}
        if(p.index.value<0)p.index.literal+=p.offset/physical;
        if(p.index.value<0 && p.index.literal>=(unsigned)count) {valid=false;break;}
        access[i]=true;indices[i]=p.index;
        if(store) {
          int k=p.index.literal;
          if(p.index.value>=0 || stores[k]>=0 ||
             !wide_table_constant(f,d,n->right,i,&table.entries[k],0)) {valid=false;break;}
          /* A pointer store transports three bytes; reproduce the load's
           * zero padding even when its initializer was an integer cast. */
          if(wide_pointer(type))table.entries[k].literal.literal&=UINT32_C(0x00ffffff);
          stores[k]=i;
        } else {
          ++loads;
          /* Keep the sampled index in its original straight-line execution.
           * A loop cannot redefine it while a captured address survives. */
          if(p.index.value>=0) {
            int at=d->definition[p.index.value];
            if(!wide_address_dominates(f,d,p.index.value,i)) {valid=false;break;}
            for(int j=at;j<i;++j)if(f->nodes[j].succ[0]!=j+1 || f->nodes[j].succ[1]>=0 ||
               f->nodes[j].jumpCount || predecessors[j+1]!=1)valid=false;
          }
        }
      }
      if(n->addressObject==object && !access[i] &&
         !(n->op==WM_ADDRESS && n->result>=0 && addresses[n->result].object==object))valid=false;
      for(unsigned k=0;k<n->orderedReadCount;++k)if(n->orderedReads[k].object==object)valid=false;
      for(int v=0;v<f->nv && valid;++v)if(addresses[v].object==object && wb_has(n->use,v)) {
        bool derived=n->result>=0 && addresses[n->result].object==object;
        if(!derived && !(access[i] && n->left.value==v && n->right.value!=v))valid=false;
      }
    }
    if(!valid || !loads)continue;
    for(int k=0;k<count && valid;++k) {
      if(stores[k]<0) {valid=false;break;}
      unsigned char without[WN_MAX];wide_address_reachable(f,d->entry,stores[k],without);
      for(int i=0;i<f->nn;++i)if(access[i] && f->nodes[i].result>=0 &&
          (!d->reachable[i] || without[i]))valid=false;
    }
    if(!valid)continue;
    unsigned tableId=f->constantTableCount++;f->constantTables[tableId]=table;
    for(int i=0;i<f->nn;++i)if(access[i]) {
      wide_node *n=&f->nodes[i];
      if(n->result<0)wide_table_nop(n);
      else {
        n->op=WM_CONSTANT_LOOKUP;n->left=indices[i];n->right=(wide_operand){.value=-1};
        n->addressIndex=(wide_operand){.value=-1};n->addressOp=0;n->addressOffset=0;
        n->addressObject=-1;n->addressSymbol=NULL;n->pointerAccess=false;n->constantTable=tableId;
        n->use=(wide_bits){{0}};wb_set(&n->use,n->left.value);
        f->values[n->result].symbolSnapshot=false;
      }
    }
    removed[object]=true;++changes;
    dbuf_printf(f->diagnostics,"; wide constant table object=%d elements=%d physical=%d loads=%u\n",object,count,physical,loads);
  }
  Safe_free(d);
  if(!changes)return 0;
  /* Delete the now-unused address, index-scaling and constant definitions;
   * memory sampling and calls remain at their original nodes. */
  bool dead;
  do {
    dead=false;wide_bits used={{0}};
    for(int i=0;i<f->nn;++i)used=wb_or(used,f->nodes[i].use);
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->result<0 || wb_has(used,n->result) || n->byteAccess || n->orderedReadCount || n->floating)continue;
      if(n->op==WM_ADDRESS || n->op=='=' || n->op==CAST || n->op=='+' || n->op=='-' ||
         n->op==WM_UMULW || n->op==LEFT_OP || n->op==BITWISEAND) {wide_table_nop(n);dead=true;}
    }
  } while(dead);
  int map[WO_MAX],objects=0,frame=0;
  for(int o=0;o<f->no;++o) {
    if(removed[o]) {map[o]=-1;continue;}
    map[o]=objects;wide_object obj=f->objects[o];obj.slot=frame;
    frame+=obj.size;f->objects[objects++]=obj;
  }
  for(int i=0;i<f->nn;++i)if(f->nodes[i].op==WM_NOP)f->nodes[i].addressObject=-1;
  wide_remap_objects(f,map);f->no=objects;f->frame=frame;f->promotedStackObjects=true;
  wide_reset_allocation(f);return changes;
}
