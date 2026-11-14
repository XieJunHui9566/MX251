"""Preserve C251 ISR bodies and place their hardware vectors with A251."""
import re


class Unsupported(ValueError):
    pass


def mask(text):
    # 不扫描字符串、注释和预处理定位行，保留所有偏移用于最小替换。
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*|^[ \t]*#[^\n]*'
    return re.sub(pattern, lambda m: re.sub(r'[^\r\n]', ' ', m[0]), text, flags=re.M)


def translate(text, maximum=63):
    if not 31 <= maximum <= 254:
        raise Unsupported('Unsupported vector capacity')
    clean = mask(text)
    if re.search(r'\b(?:_asm|__asm|asm)\b', clean):
        raise Unsupported('Inline assembly requires separate review')
    for pragma in re.findall(r'^\s*#\s*pragma\s+([^\r\n]+)', text, re.M):
        # Keil 自带头文件的调用约定作用域原样保留；向量/汇编等其它指令拒绝。
        if not re.fullmatch(r'(?:SAVE|RESTORE|PARM251|PARM4|FUNCTIONS\s*\(\s*(?:STATIC|REENTRANT)\s*\))\s*', pragma, re.I):
            raise Unsupported('Unclassified pragma: '+pragma)
    tokens = list(re.finditer(r'[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|[^\s]', clean))
    words = [m[0] for m in tokens]
    depth, found, covered = 0, [], set()
    for i, word in enumerate(words):
        if word == '{':
            depth += 1
        elif word == '}':
            depth -= 1
            if depth < 0:
                raise Unsupported('Unbalanced braces')
        if word != 'void' or depth != 0:
            continue
        j = i+1
        if j+4 >= len(words) or not re.fullmatch(r'[A-Za-z_]\w*', words[j]) or words[j+1] != '(':
            continue
        name = words[j]
        j += 2
        if words[j] == 'void':
            j += 1
        if words[j:j+2] != [')', 'interrupt']:
            continue
        irq_index = j+1
        number_index = j+2
        literal = words[number_index]
        if not re.fullmatch(r'0[xX][0-9a-fA-F]+|0|[1-9][0-9]*', literal):
            raise Unsupported('ISR number must be an expanded integer literal')
        number = int(literal, 16 if literal.lower().startswith('0x') else 10)
        if not 0 <= number <= maximum:
            raise Unsupported('ISR number outside declared vector capacity')
        j = number_index+1
        if words[j] == 'using':
            if not re.fullmatch(r'\d+', words[j+1]):
                raise Unsupported('Unknown register bank syntax')
            j += 2
        if words[j] != '{' or (i and words[i-1] not in ('}', ';')):
            raise Unsupported('ISR must be a public void definition with no arguments')
        covered.add(irq_index)
        found.append(dict(symbol=name, number=number, span=[tokens[number_index].start(), tokens[number_index].end()]))
    if depth or any(word == 'interrupt' and i not in covered for i, word in enumerate(words)):
        raise Unsupported('Unrecognized interrupt declaration or unbalanced source')
    if len(found) > 32 or len({x['number'] for x in found}) != len(found) or len({x['symbol'] for x in found}) != len(found):
        raise Unsupported('Duplicate or excessive ISR definitions')
    used = {x['number'] for x in found if x['number'] < 32}
    free = iter(i for i in range(32) if i not in used)
    for e in found:
        e['localSlot'] = e['number'] if e['number'] < 32 else next(free)
    changed = text
    for e in reversed(found):
        a, b = e['span']
        changed = changed[:a]+str(e['localSlot'])+changed[b:]
    # 独立重建原文本，确保函数体、类型、using 和 volatile 全部未动。
    restored = changed
    shift = 0
    spans = []
    for e in found:
        a, b = e['span']
        spans.append((a+shift, a+shift+len(str(e['localSlot'])), text[a:b]))
        shift += len(str(e['localSlot']))-(b-a)
    for a, b, old in reversed(spans):
        restored = restored[:a]+old+restored[b:]
    assert restored == text
    return changed, found


def absolute_vectors(text):
    """Recognize only explicit absolute three-byte numeric LJMP stubs."""
    address, result = None, []
    for line in text.splitlines():
        s = line.split(';')[0].strip()
        if not s or s.startswith('//') or s.upper() == 'END':
            continue
        m = re.fullmatch(r'CSEG\s+AT\s+([0-9a-f]+)H', s, re.I)
        if m:
            address = int(m[1], 16)
            continue
        m = re.fullmatch(r'LJMP\s+([0-9a-f]+)H', s, re.I)
        if m and address is not None and 0 <= address <= 65533:
            result.append(dict(offset=address, targetOffset=int(m[1], 16)))
            address += 3
            continue
        raise Unsupported('Unrecognized vendor assembly: '+s)
    return result


def function_code(listing):
    """Compare actual emitted instruction offsets, bytes, relocations and operands."""
    result = {}
    for name, body in re.findall(r';\s+FUNCTION (\w+) \(BEGIN\)(.*?);\s+FUNCTION \1 \(END\)', listing, re.S):
        result[name] = [line.strip() for line in body.splitlines() if re.match(r'^[0-9A-F]{6}\s+[0-9A-F]{2}', line)]
    return result
