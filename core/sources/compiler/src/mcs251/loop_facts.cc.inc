/* SPDX-License-Identifier: GPL-2.0-or-later
 * Prove bounded induction variables before iterative value propagation.
 * These facts belong to the shared iCode analysis, including backend queries.
 */
class mcs251_loop_facts
{
  std::vector<valinfos> points;
  std::map<unsigned, valinfos> headers;

  static bool defines(const iCode *ic, int key)
  {
    return ic->op != IFX && ic->op != JUMPTABLE && !POINTER_SET(ic) &&
      ic->result && IS_SYMOP(ic->result) && ic->result->key == key;
  }

  static bool scalar(const operand *op)
  {
    if (!op || !IS_SYMOP(op) || IS_OP_VOLATILE(op) ||
        !IS_INTEGRAL(operandType(op)) || bitsForType(operandType(op)) > 32)
      return false;
    const symbol *s = OP_SYMBOL_CONST(op);
    return !s->addrtaken && !IS_STATIC(s->etype) &&
      (IS_ITEMP(op) || s->islocal || s->ismyparm);
  }

  static bool literal(const iCode *ic, const operand *op, long long &number)
  {
    if (!op || !IS_OP_LITERAL(op) || !IS_INTEGRAL(operandType(op)))return false;
    valinfo v = getOperandValinfo(ic, op);
    if (v.anything || v.nothing || v.min != v.max)return false;
    number = v.min;
    return true;
  }

public:
  mcs251_loop_facts(const cfg_t &graph, ebbIndex *ebbi) : points(boost::num_vertices(graph))
  {
    const char *mode = getenv("MX251_LOOP_FACTS");
    if (!TARGET_IS_MCS251 || (mode && !strcmp(mode, "0")) || points.empty())return;
    const unsigned count = ebbi->count;
    std::map<int, unsigned> block;
    std::vector<std::vector<unsigned> > nodes(count);
    for (unsigned b = 0; b < count; ++b)
      for (iCode *ic = ebbi->bbOrder[b]->sch; ic; ic = ic == ebbi->bbOrder[b]->ech ? NULL : ic->next)
        block[ic->key] = b;
    for (unsigned n = 0; n < points.size(); ++n)nodes[block.at(graph[n].ic->key)].push_back(n);
    std::vector<std::set<unsigned> > pred(count), succ(count);
    auto edges = boost::edges(graph);
    for (auto e = edges.first; e != edges.second; ++e) {
      unsigned from = block.at(graph[boost::source(*e, graph)].ic->key);
      unsigned to = block.at(graph[boost::target(*e, graph)].ic->key);
      if (from != to || graph[boost::target(*e, graph)].ic == ebbi->bbOrder[to]->sch) {
        pred[to].insert(from); succ[from].insert(to);
      }
    }
    unsigned entry = block.at(graph[0].ic->key);
    std::set<unsigned> reachable;
    std::vector<unsigned> todo(1, entry);
    while (!todo.empty()) {
      unsigned b = todo.back(); todo.pop_back();
      if (!reachable.insert(b).second)continue;
      todo.insert(todo.end(), succ[b].begin(), succ[b].end());
    }
    std::vector<std::set<unsigned> > dom(count, reachable);
    dom[entry] = std::set<unsigned>{entry};
    bool changed;
    do {
      changed = false;
      for (unsigned b : reachable)if (b != entry) {
        std::set<unsigned> next = reachable;
        for (unsigned p : pred[b])if (reachable.count(p))
          for (auto it = next.begin(); it != next.end();)
            if (!dom[p].count(*it))it = next.erase(it); else ++it;
        next.insert(b);
        if (next != dom[b]) {dom[b].swap(next); changed = true;}
      }
    } while (changed);

    for (unsigned tail : reachable)for (unsigned head : succ[tail]) {
      if (!dom[tail].count(head) || succ[tail].size() != 2)continue;
      const iCode *branch = ebbi->bbOrder[tail]->ech;
      const iCode *compare = branch ? branch->prev : NULL;
      if (!branch || branch->op != IFX || !compare || !compare->result ||
          !isOperandEqual(branch->left, compare->result) || !scalar(compare->left))continue;
      long long bound;
      if (!literal(compare, compare->right, bound))continue;
      // Mixed comparisons may reinterpret a negative signed counter as an
      // unsigned value. Only use a bound in the counter's numeric ordering.
      if ((IS_UNSIGNED(operandType(compare->left)) && bound < 0) ||
          (!IS_UNSIGNED(operandType(compare->left)) && IS_UNSIGNED(operandType(compare->right)) &&
           bitsForType(operandType(compare->right)) >= bitsForType(operandType(compare->left))))continue;
      symbol *target = IC_TRUE(branch) ? IC_TRUE(branch) : IC_FALSE(branch);
      bool taken = eBBWithEntryLabel(ebbi, target) == ebbi->bbOrder[head];
      bool truth = taken == !!IC_TRUE(branch);
      int relation = compare->op;
      if (!truth) {
        if (relation == '<')relation = GE_OP;
        else if (relation == '>')relation = LE_OP;
        else if (relation == LE_OP)relation = '>';
        else if (relation == GE_OP)relation = '<';
        else continue;
      }
      if (relation != '<' && relation != '>' && relation != LE_OP && relation != GE_OP)continue;

      // Natural loop, with exactly this guarded backedge and no side entry.
      std::set<unsigned> loop{head};
      todo.assign(1, tail);
      while (!todo.empty()) {
        unsigned b = todo.back(); todo.pop_back();
        if (!loop.insert(b).second)continue;
        for (unsigned p : pred[b])if (reachable.count(p))todo.push_back(p);
      }
      bool valid = true;
      for (unsigned b : loop) {
        if (!dom[b].count(head))valid = false;
        for (unsigned p : pred[b])if (reachable.count(p) &&
            ((!loop.count(p) && b != head) || (b == head && loop.count(p) && p != tail)))valid = false;
      }
      if (!valid)continue;
      // Removing the backedge must leave a DAG: the update cannot execute
      // repeatedly inside a second cycle before reaching the loop test.
      std::map<unsigned, unsigned> degree;
      for (unsigned b : loop) {
        degree[b] = 0;
        for (unsigned p : pred[b])if (loop.count(p) && !(p == tail && b == head))++degree[b];
      }
      todo.clear();
      for (const auto &d : degree)if (!d.second)todo.push_back(d.first);
      unsigned visited = 0;
      while (!todo.empty()) {
        unsigned b = todo.back(); todo.pop_back(); ++visited;
        for (unsigned s : succ[b])if (loop.count(s) && !(b == tail && s == head) && !--degree[s])todo.push_back(s);
      }
      if (visited != loop.size())continue;

      int key = compare->left->key;
      const iCode *update = NULL;
      unsigned updateBlock = count;
      for (unsigned b : loop)for (unsigned n : nodes[b]) {
        const iCode *ic = graph[n].ic;
        if (ic->op == INLINEASM)valid = false;
        if (defines(ic, key)) {
          if (update)valid = false;
          update = ic; updateBlock = b;
        }
      }
      long long step;
      if (!valid || !update || !dom[tail].count(updateBlock) ||
          (update->op != '+' && update->op != '-') ||
          !isOperandEqual(update->left, compare->left) ||
          !literal(update, update->right, step))continue;
      if (update->op == '-')step = -step;
      if (!step || (step > 0 && relation != '<' && relation != LE_OP) ||
          (step < 0 && relation != '>' && relation != GE_OP))continue;
      // The compare must see the updated value (also for one-block loops).
      if (updateBlock == tail) {
        bool seen = false;
        for (unsigned n : nodes[tail]) {
          if (graph[n].ic == update)seen = true;
          if (graph[n].ic == compare && !seen)valid = false;
        }
      }
      if (!valid)continue;

      // Find a constant definition on the unique preheader chain. This
      // accepts normal rotated for/while loops without relying on C names.
      std::vector<unsigned> outside;
      for (unsigned p : pred[head])if (reachable.count(p) && !loop.count(p))outside.push_back(p);
      if (outside.size() != 1)continue;
      std::set<unsigned> seen;
      unsigned b = outside[0];
      long long initial = 0;
      bool initialized = false;
      while (seen.insert(b).second && !loop.count(b)) {
        for (auto n = nodes[b].rbegin(); n != nodes[b].rend(); ++n) {
          const iCode *ic = graph[*n].ic;
          if (ic->op == INLINEASM) {valid = false; break;}
          if (defines(ic, key)) {
            initialized = ic->op == '=' && literal(ic, ic->right, initial);
            valid &= initialized;
            break;
          }
        }
        if (!valid || initialized || b == entry)break;
        outside.clear();
        for (unsigned p : pred[b])if (reachable.count(p))outside.push_back(p);
        if (outside.size() != 1)break;
        b = outside[0];
      }
      if (!valid || !initialized)continue;
      valinfo fact = getTypeValinfo(operandType(compare->left), false);
      if (fact.anything || initial < fact.min || initial > fact.max)continue;
      // Include the final updated value, which is visible at the test and
      // on exit. Check its representability even for unsigned induction.
      long long low = initial, high = initial;
      if (step > 0)high = std::max(initial, bound - (relation == '<')) + step;
      else low = std::min(initial, bound + (relation == '>')) + step;
      if (low < fact.min || high > fact.max)continue;
      fact.min = low; fact.max = high;
      fact.knownbits = fact.knownbitsmask = 0;
      valinfoUpdate(&fact);
      for (unsigned body : loop)for (unsigned n : nodes[body])points[n].map[key] = fact;
      headers[nodes[head].front()].map[key] = fact;
    }
  }

  void seed(unsigned node, valinfos &values) const
  {
    auto found = headers.find(node);
    if (found != headers.end())values = found->second;
  }

  bool merge(unsigned node, iCode *ic, const valinfos &incoming) const
  {
    bool changed = false;
    for (const auto &entry : incoming.map) {
      valinfo v = entry.second;
      auto found = points[node].map.find(entry.first);
      if (found != points[node].map.end() && !v.nothing) {
        const valinfo &fact = found->second;
        if (v.anything || v.min > fact.max || v.max < fact.min)v = fact;
        else {
          v.min = std::max(v.min, fact.min); v.max = std::min(v.max, fact.max);
          valinfoUpdate(&v);
        }
      }
      changed |= valinfos_union(ic, entry.first, v);
    }
    return changed;
  }
};
