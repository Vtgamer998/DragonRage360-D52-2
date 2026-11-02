#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Separate intact, unreferenced PS2 library data from native game data.

No callback is replaced or zeroed. The normal --gc-sections linker decides
whether to retain the legacy sections. Explicitly rooting their labels must
still fail if their PS2 library functions have no native implementation.
Only the two audited files of the pinned upstream are accepted.
"""
from pathlib import Path
import hashlib, json, struct, subprocess
from build_overlay_catalog import read_object

RULES = {
    'port/data/cod_1C3400.data.s': {
        'sha256': 'd8d6135cac3d10cf2e75f65e1d737176bd50cb22accb1876dbc33911094628be',
        'cut': 0x3d00, 'section': '.data.xb_ps2_library',
        'label': 'D_002C7100', 'rename': {}},
    'port/data/cod_1F44C0.rodata.s': {
        'sha256': 'dd32418dd6ce892716999d639643a3b2ee0e89d19489a84507d0695a33091f16',
        'cut': 0, 'section': '.data.xb_ps2_library_rodata',
        'rename': {'_ctype_': '__xb_ps2_ctype_table'}}}

def prepare(rel, text):
    if rel not in RULES:
        return text
    rule = RULES[rel]
    if hashlib.sha256(text.encode()).hexdigest() != rule['sha256']:
        raise ValueError('unreviewed data skeleton: ' + rel)
    section = '.section ' + rule['section'] + ',"aw",@progbits'
    if rule['cut']:
        anchor = '.globl ' + rule['label'] + '\n'
        if text.count(anchor) != 1:
            raise ValueError('ambiguous data boundary')
        text = text.replace(anchor, section + '\n.p2align 4\n' + anchor)
    else:
        if text.count('.section .data\n') != 1:
            raise ValueError('unexpected section shape')
        text = text.replace('.section .data\n', section + '\n')
    for before, after in rule['rename'].items():
        # Keep the PS2 table intact under a private name. Native C's ctype
        # macros must bind the SDK libc table, not this uninitialized skeleton.
        if text.count('.globl ' + before + '\n') != 1:
            raise ValueError('missing libc collision symbol')
        text = text.replace('.globl ' + before + '\n', '.globl ' + after + '\n')
        text = text.replace('\n' + before + ':\n', '\n' + after + ':\n')
    return text

def model(path, rule=None):
    b, _, sections, names, symbols = read_object(path)
    bases = {'.data': 0}
    rename = {}
    if rule:
        bases[rule['section']] = rule['cut']
        rename = {v: k for k, v in rule['rename'].items()}
    chunks, labels, relocs = [], {}, []
    for i, s in enumerate(sections):
        if names[i] in bases and s[5]:
            chunks.append((bases[names[i]], b[s[4]:s[4]+s[5]]))
    chunks.sort()
    at = 0
    for off, data in chunks:
        if off != at:
            raise ValueError('changed data extent')
        at += len(data)
    for name, value, size, info, shndx in symbols:
        if name and shndx < len(sections) and names[shndx] in bases:
            labels[rename.get(name, name)] = (bases[names[shndx]] + value, size, info)
    for s in sections:
        if s[1] != 4 or names[s[7]] not in bases:
            continue
        if s[9] != 12:
            raise ValueError('unexpected relocation width')
        for off in range(s[4], s[4]+s[5], 12):
            where, info, addend = struct.unpack_from('>IIi', b, off)
            target = symbols[info >> 8][0]
            if not target:
                raise ValueError('unnamed data relocation')
            relocs.append((bases[names[s[7]]] + where, rename.get(target, target), info & 255, addend))
    return b''.join(data for _, data in chunks), labels, sorted(relocs)

def audit(rows, cc, out):
    """Prove unchanged bytes/labels/relocations and reject native consumers.

    The check covers *all* compiled allocated relocation sites, including
    code later GC-discarded. It cannot certify untyped runtime PS2 pointers;
    those still require the native data manifest before any game boot.
    """
    report = {'objects': [], 'incoming_native_relocations': [],
              'tables_deleted_from_objects': False, 'callbacks_replaced': False,
              'PS2_runtime_pointer_manifest_complete': False}
    owned = {}
    for row in rows:
        rule = RULES.get(row['source'])
        if not rule:
            continue
        if not row['compiled']:
            raise ValueError('data section compilation failed')
        path = Path(row['object'])
        original = path.with_suffix('.original-audit.o')
        source = out/'prepared'/(path.stem+'.original-audit.s')
        source.write_text(Path(row['original_source']).read_text())
        subprocess.run([str(cc), '-mcpu=cell', '-c', str(source), '-o', str(original)], check=True)
        before = model(original)
        after = model(path, rule)
        if before != after:
            raise ValueError('native section transform changed data or relocations')
        b, _, sections, names, symbols = read_object(path)
        legacy = {i for i, name in enumerate(names) if name == rule['section']}
        for name, value, size, info, shndx in symbols:
            if name and shndx in legacy:
                owned[name] = (str(path), shndx)
        refs = sorted({target for where, target, kind, addend in before[2]
                       if where >= rule['cut']})
        report['objects'].append({'source': row['source'], 'section': rule['section'],
            'cut': rule['cut'], 'total_bytes': len(before[0]),
            'legacy_bytes': sum(sections[i][5] for i in legacy),
            'labels_preserved': len(before[1]), 'relocations_preserved': len(before[2]),
            'original_bytes_sha256': hashlib.sha256(before[0]).hexdigest(),
            'all_bytes_labels_relocations_identical': True,
            'renamed_sdk_collision': rule['rename'], 'legacy_targets': refs})
    for row in rows:
        b, _, sections, names, symbols = read_object(Path(row['object']))
        for s in sections:
            if s[1] != 4 or not sections[s[7]][2] & 2:
                continue
            for at in range(s[4], s[4]+s[5], 12):
                offset, info, addend = struct.unpack_from('>IIi', b, at)
                name, value, size, binding, shndx = symbols[info >> 8]
                target_owner = owned.get(name)
                if not target_owner:
                    continue
                # A legacy section may refer to the other legacy section.
                rule = RULES.get(row['source'])
                if rule and names[s[7]] == rule['section']:
                    continue
                report['incoming_native_relocations'].append({
                    'source': row['source'], 'section': names[s[7]],
                    'offset': offset, 'target': name, 'addend': addend})
    (out/'native-data-sections.json').write_text(json.dumps(report, indent=2)+'\n')
    if report['incoming_native_relocations']:
        raise ValueError('native reference to a PS2 library section: review required')
    return report
