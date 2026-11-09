import copy
from types import SimpleNamespace
import unittest

from selective_leaf_abi import only_direct_references, select_candidates


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.unit = dict(name='u0', definitions={'_write': 'v(u8,u8)'},
                         abi={'_write': (2, 0, 0)}, flags={'_write': dict(
                             isr='0', addressTaken='0', opaque='0', indirect='0')},
                         lines=['.globl _write', '_write:', '\teret'], instructions=[
                             SimpleNamespace(owner='_write', source='eret', address=1)])
        self.calls = []
        for i in range(16):
            self.unit['instructions'].append(SimpleNamespace(owner='_caller', source='ecall _write', address=10 + 4*i))
            self.unit['lines'].append('ecall _write')
            self.calls.append(dict(unit='u0', owner='_caller', target='_write', signature='v(u8,u8)',
                                   address=10 + 4*i, layouts={'abi4': dict(grossPushCodeBytes=2, grossCleanupCodeBytes=2)}))

    def select(self, units=None, calls=None, refs=(), startup=''):
        return select_candidates(units or [self.unit], self.calls if calls is None else calls,
                                 set(refs), startup, 64)

    def test_closed_leaf(self):
        result = self.select()
        self.assertEqual([(x['symbol'], x['transport'], x['calls']) for x in result],
                         [('_write', 'void(u8,u8)', 16)])

    def test_untyped_call_and_library_or_startup_entry(self):
        self.assertFalse(self.select(calls=self.calls[:-1]))
        self.assertFalse(self.select(refs=['_write']))
        self.assertFalse(self.select(startup='ecall _write'))

    def test_address_escape_and_duplicate_definition(self):
        self.assertTrue(only_direct_references('; _write\n.globl _write\n_write:\necall _write', '_write'))
        self.assertFalse(only_direct_references('mov dptr,#_write', '_write'))
        self.unit['lines'].append('.dw _write')
        self.assertFalse(self.select())
        self.unit['lines'].pop()
        self.assertFalse(self.select(units=[self.unit, copy.deepcopy(self.unit)]))

    def test_not_leaf_and_wrong_abi(self):
        self.unit['instructions'].append(SimpleNamespace(owner='_write', source='ecall _other', address=3))
        self.assertFalse(self.select())
        self.unit['instructions'].pop()
        self.unit['abi']['_write'] = (4, 0, 0)
        self.assertFalse(self.select())


if __name__ == '__main__':
    unittest.main()
