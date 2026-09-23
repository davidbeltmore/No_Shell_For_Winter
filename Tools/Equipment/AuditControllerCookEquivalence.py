"""Offline, fail-closed audit of the two frozen ACF 4.4.2 controller programs.
Reads ProjectCalystoScriptFingerprint canonical archives. Unknown opcodes,
unreachable executable instructions, changed calls/operands/branches or a
non-frozen Editor input fail. Never edits runtime fingerprints or assets.
Only tracing, jumps to equivalent blocks and compiler temporary names normalize.
"""
import pathlib, struct, json, re, sys

class Reader:

    def __init__(self, data):
        self.data = data
        self.p = 0
        self.vm = 0

    def val(self, fmt):
        n = struct.calcsize('<' + fmt)
        x = struct.unpack_from('<' + fmt, self.data, self.p)[0]
        self.p += n
        self.vm += n
        return x

    def string(self, vm_size=0):
        old = self.vm
        n = self.val('i')
        assert abs(n) < 100000
        size = abs(n) * (2 if n < 0 else 1)
        v = self.data[self.p:self.p + size].decode('utf-16-le' if n < 0 else 'ascii').rstrip('\x00')
        self.p += size
        self.vm = old + vm_size
        return v

    def expr(self):
        start = self.vm
        op = self.val('B')
        a = []
        if op in (0, 1, 2, 72, 51, 32, 108):
            a = [self.string(8)]
        elif op in (29, 6, 76, 91):
            a = [self.val('i')]
        elif op in (36, 44):
            a = [self.val('B')]
        elif op in (27, 69, 28, 70, 104, 99):
            a = [self.string(12 if op in (27, 69) else 8)]
            params = []
            while True:
                e = self.expr()
                if e['op'] == 22:
                    break
                params.append(e)
            a.append(params)
        elif op in (25, 26, 18):
            obj = self.expr()
            skip = self.val('i')
            field = self.string(8)
            before = self.vm
            e = self.expr()
            assert self.vm - before == skip, (before, skip, self.vm)
            a = [obj, field, e]
        elif op in (15, 95, 96, 20, 67, 68, 92, 98):
            if op == 15:
                a.append(self.string(8))
            a.extend([self.expr(), self.expr()])
        elif op in (100, 66):
            a = [self.string(8), self.expr()]
        elif op in (4, 78, 79, 81, 93):
            a = [self.expr()]
        elif op == 7:
            a = [self.val('i'), self.expr()]
        elif op in (33, 75):
            a = [self.string(12)]
        elif op == 97:
            a = [self.string(12), self.expr(), self.expr()]
        elif op == 49:
            a = [self.expr()]
            while True:
                e = self.expr()
                if e['op'] == 50:
                    break
                a.append(e)
        elif op in (11, 83, 22, 21, 23, 37, 38, 39, 40, 42, 45, 77, 90, 94, 50):
            pass
        else:
            raise ValueError(('unsupported', hex(op), start, self.p))
        return {'offset': start, 'end': self.vm, 'op': op, 'args': a}

    def read(self):
        cls = self.string()
        parent = self.string()
        fs = {}
        while self.p < len(self.data):
            name = self.string()
            size = self.val('i')
            self.vm = 0
            es = []
            while self.vm < size:
                es.append(self.expr())
            assert self.vm == size
            fs[name] = es
        return {'class': cls, 'parent': parent, 'functions': fs}
import json, pathlib, re, sys, difflib

def normalize(data):
    fs = data['functions']
    reached = set()
    result = {}
    for root, es in fs.items():
        if root.startswith('ExecuteUbergraph_'):
            continue
        aliases = {}

        def value(x):
            if isinstance(x, dict):
                op = x['op']
                a = x['args']
                if op in (70, 28) and 'ExecuteUbergraph_' in a[0]:
                    name = a[0].split(':')[-1]
                    assert len(a[1]) == 1 and a[1][0]['op'] == 29
                    return ['ubergraph', trace(name, a[1][0]['args'][0], frozenset())]
                return [hex(op), *[value(v) for v in a]]
            if isinstance(x, list):
                return [value(v) for v in x]
            if isinstance(x, str):
                leaf = x.rsplit(':', 1)[-1]
                if re.match('(CallFunc_|K2Node_CreateDelegate_)', leaf):
                    if x not in aliases:
                        aliases[x] = 'temporary_' + str(len(aliases))
                    return aliases[x]
            return x

        def trace(fn, offset, seen):
            nodes = {n['offset']: n for n in fs[fn]}
            out = []
            while True:
                assert (fn, offset) not in seen, ('cycle', fn, offset)
                seen = seen | {(fn, offset)}
                n = nodes[offset]
                reached.add((fn, offset))
                op = n['op']
                if op in (94, 90):
                    offset = n['end']
                    continue
                if op == 6:
                    offset = n['args'][0]
                    continue
                if op == 7:
                    return out + [['branch', value(n['args'][1]), trace(fn, n['end'], seen), trace(fn, n['args'][0], seen)]]
                out.append(value(n))
                if op in (4, 83):
                    return out
                offset = n['end']
        result[root] = trace(root, 0, frozenset())
    omitted = []
    for fn, es in fs.items():
        for n in es:
            if (fn, n['offset']) not in reached:
                dispatcher = fn.startswith('ExecuteUbergraph_') and n['offset'] == 0 and (n['op'] == 78)
                if n['op'] not in (90, 94, 83) and (not dispatcher):
                    omitted.append((fn, n['offset'], n['op']))
    assert not omitted, omitted
    return dict(class_path=data['class'], parent=data['parent'], functions=result)
FROZEN = {'ACF_NPCController_BP_C': '17FF44E72BB1D1510003BAA3A62673440322EED6', 'ACFAIControllerBP_C': 'C173DC40FA2886C07128355F3E0664F35E0FBE4C'}

def audit(editor, cooked):
    import hashlib
    editor_bytes, cooked_bytes = (editor.read_bytes(), cooked.read_bytes())
    x, y = (Reader(editor_bytes).read(), Reader(cooked_bytes).read())
    name = x['class'].split('.')[-1]
    editor_hash = hashlib.sha1(editor_bytes).hexdigest().upper()
    assert editor_hash == FROZEN[name], 'Editor archive differs from frozen audit'
    assert normalize(x) == normalize(y), 'Cooked program differs in control flow or operands'
    return dict(class_name=name, frozen_editor_sha1=editor_hash, cooked_sha1=hashlib.sha1(cooked_bytes).hexdigest().upper(), semantic_control_flow_and_operands_equal=True, functions=list(normalize(x)['functions']), editor_archive=str(editor), cooked_archive=str(cooked))
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('editor', type=pathlib.Path)
    parser.add_argument('cooked', type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.editor, args.cooked), indent=2))
