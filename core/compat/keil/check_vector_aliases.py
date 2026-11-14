"""Exercise numeric and shared-label C251 interrupt redirects."""
from vector_aliases import translate

numeric = 'CSEG AT 012BH\nJMP 006BH\nEND'
shared = '''CSEG AT 012BH
JMP first
CSEG AT 0133H
JMP second
first:
second:
JMP tail
tail:
LJMP 006BH
END
'''
assert translate(numeric) == translate('CSEG AT 012BH\nJMP first\nfirst:\nJMP 006BH\nEND')
assert translate(numeric) == translate('CSEG AT 012BH\nJMP abh\nabh:\nJMP 006BH\nEND\nignored')
text = translate(shared)
assert text.count('__interrupt(') == 2
assert '__interrupt(37)' in text and '__interrupt(38)' in text
assert text.count('ejmp 0xff006b') == 2
for bad in (
    'CSEG AT 012BH\nJMP missing',
    'CSEG AT 012BH\nJMP a\na:\nJMP b\nb:\nJMP a',
    'CSEG AT 012BH\nJMP a\na:',
    'CSEG AT 012BH\nJMP 006BH\nCSEG AT 012BH\nJMP 006BH',
    'CSEG AT 012BH\nMOV A,#1\nJMP 006BH',
    'CSEG AT 012BH\nJMP 006CH',
    'CSEG AT 012CH\nJMP 006BH',
    'CSEG AT 012BH\nJMP a\na:\na:\nJMP 006BH',
):
    assert translate(bad) is None, bad
print('PASS numeric/shared-label redirects and 8 unsupported graphs')
