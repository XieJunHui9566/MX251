/* SPDX-License-Identifier: GPL-2.0-or-later
 * Available integer expressions retain the relationship between a snapshot
 * and values computed from it. Refine those values on individual CFG edges,
 * before the shared constant folder and either register allocator run.
 */
class mcs251_branch_facts
{
  typedef std::map<int, const iCode *> expressions;
  std::map<const iCode *, expressions> branches;

  static bool scalar(const operand *op)
  {
    if (!op || !IS_INTEGRAL(operandType(op)) || bitsForType(operandType(op)) > 32 || IS_OP_VOLATILE(op))
      return false;
    if (IS_OP_LITERAL(op))
      return true;
    if (!IS_SYMOP(op))
      return false;
    // A temporary loaded from memory is a snapshot. Direct memory operands
    // and address-taken locals can change without an iCode definition.
    const symbol *s = OP_SYMBOL_CONST(op);
    return IS_ITEMP(op) || ((s->islocal || s->ismyparm) && !s->addrtaken && !IS_STATIC(s->etype));
  }

  static bool expression(const iCode *ic)
  {
    if (!scalar(ic->result) || !IS_SYMOP(ic->result) || POINTER_SET(ic))
      return false;
    if (ic->op == '=' || ic->op == CAST)
      return scalar(ic->right);
    switch(ic->op) {
    case '+':case '-':case BITWISEAND:case '|':case '^':case LEFT_OP:case RIGHT_OP:
    case EQ_OP:case NE_OP:case '<':case '>':case LE_OP:case GE_OP:
      return scalar(ic->left) && scalar(ic->right);
    default:return false;
    }
  }

  static bool uses(const iCode *ic,int key)
  {
    return (ic->op!=CAST && ic->left && IS_SYMOP(ic->left) && ic->left->key==key) ||
      (ic->right && IS_SYMOP(ic->right) && ic->right->key==key);
  }

  static valinfo value(const iCode *branch,const operand *op,const valinfos &edge)
  {
    if(IS_SYMOP(op)) {
      auto it=edge.map.find(op->key);
      if(it!=edge.map.end())return it->second;
    }
    return getOperandValinfo(branch,op);
  }

  static bool narrow(const iCode *branch,const operand *op,const valinfo &fact,valinfos &edge)
  {
    if (!scalar(op) || !IS_SYMOP(op) || fact.anything || fact.nothing)
      return false;
    valinfo before = value(branch,op,edge), after = before;
    if (before.anything || before.nothing)
      return false;
    after.min = std::max(before.min,fact.min);
    after.max = std::min(before.max,fact.max);
    if (after.min > after.max)
      return false;
    if(after.min>=0 && fact.min>=0) {
      if((before.knownbits^fact.knownbits)&before.knownbitsmask&fact.knownbitsmask)return false;
      after.knownbits=(before.knownbits&before.knownbitsmask)|(fact.knownbits&fact.knownbitsmask);
      after.knownbitsmask=before.knownbitsmask|fact.knownbitsmask;
    } else {after.knownbitsmask=after.knownbits=0;}
    valinfoUpdate(&after);
    if(after.min>after.max)return false;
    if(after.min==before.min && after.max==before.max &&
       after.knownbitsmask==before.knownbitsmask &&
       (after.knownbits&after.knownbitsmask)==(before.knownbits&before.knownbitsmask))return false;
    edge.map[op->key] = after;
    return true;
  }

  static bool equal(const iCode *branch,const operand *op,long long number,valinfos &edge)
  {
    if (!scalar(op) || !IS_SYMOP(op))
      return false;
    valinfo fact = getTypeValinfo(operandType(op),true);
    if (fact.anything || number < fact.min || number > fact.max)
      return false;
    fact.min = fact.max = number;
    valinfoUpdate(&fact);
    return narrow(branch,op,fact,edge);
  }

  // Invert a copy/cast or a constant offset only when its current source
  // range contains exactly one preimage. This includes normalized switch
  // indices and excludes ambiguous truncations such as (u8)a == 1 for u16 a.
  static bool preimage(const iCode *branch,const iCode *ic,const operand *source,
                       long long number,valinfos &edge)
  {
    if (IS_BOOLEAN(operandType(ic->result)))
      return false;
    valinfo range = value(branch,source,edge);
    if (range.anything || range.nothing)
      return false;
    long long modulus = 1ll << bitsForType(operandType(ic->result));
    long long candidate = range.min + ((number-range.min)%modulus+modulus)%modulus;
    if (candidate > range.max || candidate+modulus <= range.max)
      return false;
    return equal(branch,source,candidate,edge);
  }

  static void replay(const iCode *branch,const expressions &available,valinfos &edge)
  {
    bool changed;
    do {
      changed=false;
      for(const auto &entry:available) {
        const iCode *ic=entry.second;
        valinfo known=value(branch,ic->result,edge);
        valinfo right=value(branch,ic->right,edge);
        if(!known.anything && !known.nothing && known.min==known.max) {
          if(ic->op=='=' || ic->op==CAST)
            changed|=preimage(branch,ic,ic->right,known.min,edge);
          else if(ic->op=='+' || ic->op=='-') {
            valinfo left=value(branch,ic->left,edge);
            if(!right.anything && !right.nothing && right.min==right.max)
              changed|=preimage(branch,ic,ic->left,known.min+(ic->op=='+'?-right.min:right.min),edge);
            if(!left.anything && !left.nothing && left.min==left.max)
              changed|=preimage(branch,ic,ic->right,ic->op=='+'?known.min-left.min:left.min-known.min,edge);
          }
          right=value(branch,ic->right,edge);
        }
        valinfo result=getTypeValinfo(operandType(ic->result),true);
        if(ic->op=='=' || ic->op==CAST)
          valinfoCast(&result,operandType(ic->result),right,operandType(ic->right));
        else {
          valinfo left=value(branch,ic->left,edge);
          switch(ic->op) {
          case '+':valinfoPlus(&result,operandType(ic->result),left,right);break;
          case '-':valinfoMinus(&result,operandType(ic->result),left,right);break;
          case BITWISEAND:valinfoAnd(&result,operandType(ic->result),left,right);break;
          case '|':valinfoOr(&result,left,right);break;
          case '^':valinfoXor(&result,left,right);break;
          case LEFT_OP:valinfoLeft(&result,left,right);break;
          case RIGHT_OP:valinfoRight(&result,left,right);break;
          default:continue; // Comparison results are handled by the existing folder.
          }
        }
        valinfoUpdate(&result);
        changed|=narrow(branch,ic->result,result,edge);
      }
    } while(changed);
  }

public:
  mcs251_branch_facts(const cfg_t &graph,ebbIndex *ebbi)
  {
    const char *mode = getenv("MX251_BRANCH_FACTS");
    if (!TARGET_IS_MCS251 || (mode && !strcmp(mode,"0")))
      return;
    const unsigned count = ebbi->count;
    std::map<int,unsigned> block;
    for(unsigned b=0;b<count;++b)
      for(iCode *ic=ebbi->bbOrder[b]->sch;ic;ic=ic==ebbi->bbOrder[b]->ech?NULL:ic->next)
        block[ic->key]=b;
    std::vector<std::set<unsigned> > predecessors(count),successors(count);
    auto edges=boost::edges(graph);
    for(auto e=edges.first;e!=edges.second;++e) {
      unsigned from=block.at(graph[boost::source(*e,graph)].ic->key);
      unsigned to=block.at(graph[boost::target(*e,graph)].ic->key);
      // Keep a one-block loop's back edge, but not instruction fallthrough.
      if(from!=to || graph[boost::target(*e,graph)].ic==ebbi->bbOrder[to]->sch) {
        predecessors[to].insert(from);successors[from].insert(to);
      }
    }
    unsigned entry=block.at(graph[0].ic->key);
    std::vector<expressions> output(count);
    std::vector<bool> known(count,false),queued(count,false);
    std::queue<unsigned> todo;
    todo.push(entry);
    queued[entry]=true;
    while(!todo.empty()) {
      unsigned b=todo.front();
      todo.pop();
      queued[b]=false;
      expressions current;
      bool first=true;
      // Intersect definitions from every reachable predecessor. An unseen
      // predecessor participates when processed and reschedules successors.
      // The entry always has the function's empty incoming expression set.
      if(b!=entry)for(unsigned p:predecessors[b])if(known[p]) {
        if(first) {current=output[p];first=false;}
        else for(auto it=current.begin();it!=current.end();) {
          auto found=output[p].find(it->first);
          if(found==output[p].end() || found->second!=it->second)it=current.erase(it);else ++it;
        }
      }
      for(iCode *ic=ebbi->bbOrder[b]->sch;ic;ic=ic==ebbi->bbOrder[b]->ech?NULL:ic->next) {
        if(ic->op==IFX || ic->op==JUMPTABLE) {
          branches[ic]=current;
          continue;
        }
        if(ic->op==INLINEASM) {
          current.clear();
          continue;
        }
        if(!ic->result || !IS_SYMOP(ic->result) || POINTER_SET(ic))continue;
        int key=ic->result->key;
        for(auto it=current.begin();it!=current.end();)
          if(it->first==key || uses(it->second,key))it=current.erase(it);else ++it;
        // x = x + 1 relates an old version of x to the new one; this map
        // records relationships between current operands only.
        if(expression(ic) && !uses(ic,key))current[key]=ic;
      }
      if(known[b] && current==output[b])continue;
      known[b]=true;output[b].swap(current);
      for(unsigned s:successors[b])if(!queued[s]) {todo.push(s);queued[s]=true;}
    }
  }

  void refine(cfg_t &graph,unsigned node) const
  {
    const iCode *branch=graph[node].ic;
    auto found=branches.find(branch);
    if(found==branches.end())return;
    const expressions &available=found->second;
    auto edges=boost::out_edges(node,graph);
    unsigned ordinal=0;
    for(auto e=edges.first;e!=edges.second;++e,++ordinal) {
      valinfos &edge=graph[*e];
      if(branch->op==JUMPTABLE)equal(branch,branch->left,ordinal,edge);
      else {
        // CFG construction inserts fallthrough first, then the taken edge.
        // Using edge identity also covers both edges targeting one label.
        bool truth=ordinal?IC_TRUE(branch)!=NULL:IC_FALSE(branch)!=NULL;
        if(!truth)equal(branch,branch->left,0,edge);
        auto definition=available.find(branch->left->key);
        if(definition!=available.end()) {
          const iCode *compare=definition->second;
          if((compare->op==EQ_OP && truth) || (compare->op==NE_OP && !truth)) {
            valinfo left=value(branch,compare->left,edge),right=value(branch,compare->right,edge);
            if(!right.anything && !right.nothing && right.min==right.max)equal(branch,compare->left,right.min,edge);
            if(!left.anything && !left.nothing && left.min==left.max)equal(branch,compare->right,left.min,edge);
          }
        }
      }
      replay(branch,available,edge);
    }
  }
};
