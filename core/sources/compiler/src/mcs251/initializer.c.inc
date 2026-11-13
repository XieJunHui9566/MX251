/* Normalize positional brace elision before array bounds are inferred.
 * Designated lists retain the existing designated-initializer path. */
static bool
mcs251HasDesignators (initList *list)
{
  for (; list; list = list->next)
    if (list->designation ||
        (list->type == INIT_DEEP && mcs251HasDesignators (list->init.deep)))
      return true;
  return false;
}

static bool
mcs251WholeAggregateValue (sym_link *type, initList *list)
{
  if (list->type != INIT_NODE)
    return false;
  ast *node = list->init.node;
  sym_link *source = node->ftype;
  if (!source && node->type == EX_VALUE)
    source = node->opval.val->type;
  if (!source)
    return false;
  if (IS_STRUCT (type) && IS_STRUCT (source))
    return SPEC_STRUCT (type) == SPEC_STRUCT (source);
  return IS_ARRAY (type) && IS_ARRAY (source) &&
    IS_CHAR (type->next) && IS_CHAR (source->next);
}

static initList *mcs251TakeInitializer (sym_link *type, initList **cursor);

static initList *
mcs251AggregateMembers (sym_link *type, initList **cursor)
{
  initList *head = NULL, **tail = &head;
  if (IS_ARRAY (type))
    {
      unsigned int count = DCL_ELEM (type);
      for (unsigned int i = 0; *cursor && (!count || i < count); ++i)
        {
          initList *before = *cursor;
          *tail = mcs251TakeInitializer (type->next, cursor);
          if (*cursor == before)
            break;
          tail = &(*tail)->next;
        }
    }
  else
    for (symbol *field = SPEC_STRUCT (type)->fields; field && *cursor; field = field->next)
      {
        if (IS_BITFIELD (field->type) && SPEC_BUNNAMED (field->etype))
          continue;
        *tail = mcs251TakeInitializer (field->type, cursor);
        tail = &(*tail)->next;
        if (SPEC_STRUCT (type)->type == UNION)
          break;
      }
  return head;
}

static initList *
mcs251TakeInitializer (sym_link *type, initList **cursor)
{
  initList *item = *cursor;
  if (!IS_AGGREGATE (type) || item->type != INIT_NODE ||
      mcs251WholeAggregateValue (type, item))
    {
      *cursor = item->next;
      item->next = NULL;
      if (IS_AGGREGATE (type) && item->type == INIT_DEEP && !item->isempty)
        {
          /* A braced character string initializes the whole array. */
          if (IS_ARRAY (type) && item->init.deep &&
              mcs251WholeAggregateValue (type, item->init.deep))
            return item;
          initList *inner = item->init.deep;
          item->init.deep = mcs251AggregateMembers (type, &inner);
          /* Keep excess elements for the existing diagnostics. */
          initList **tail = &item->init.deep;
          while (*tail) tail = &(*tail)->next;
          *tail = inner;
        }
      return item;
    }
  initList *group = newiList (INIT_DEEP, NULL);
  group->filename = item->filename;
  group->lineno = item->lineno;
  group->init.deep = mcs251AggregateMembers (type, cursor);
  return group;
}

void
mcs251NormalizeInitializer (sym_link *type, initList *list)
{
  if (!TARGET_IS_MCS251 || !IS_AGGREGATE (type) || !list ||
      list->type != INIT_DEEP || list->isempty || mcs251HasDesignators (list))
    return;
  initList *cursor = list;
  mcs251TakeInitializer (type, &cursor);
}
