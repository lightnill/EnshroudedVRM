"""Bounded PE32+ export metadata and explicit Windows-loader forwarder definitions.

This reads export metadata, never redistributes the provider's code. Forwarding is
resolved by Windows; there is no lazy LoadLibrary trampoline in a caller's DllMain.
"""
from pathlib import Path
import hashlib
import json
import re
import struct



def export_inventory(path):
    data = Path(path).read_bytes()
    if not 256 <= len(data) <= 32 * 1024 * 1024 or data[:2] != b'MZ':
        raise ValueError('invalid bounded PE file')

    def read(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('PE metadata outside file')
        return struct.unpack_from(fmt, data, offset)

    pe, = read('<I', 0x3c)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('invalid PE signature')
    machine, sections = read('<HH', pe+4)
    optional_size, = read('<H', pe+20)
    optional = pe+24
    if machine != 0x8664 or not 1 <= sections <= 96 or optional_size < 120 or read('<H', optional)[0] != 0x20b:
        raise ValueError('expected bounded x64 PE32+')
    if read('<I', optional+108)[0] < 1:
        raise ValueError('PE has no export directory')
    export_rva, export_size = read('<II', optional+112)
    table = optional+optional_size
    raw_sections = []
    for index in range(sections):
        virtual_size, address, raw_size, pointer = read('<IIII', table+40*index+8)
        if pointer + raw_size > len(data):
            raise ValueError('invalid section extent')
        raw_sections.append((address, raw_size, pointer))

    def offset(rva, length):
        for address, raw_size, pointer in raw_sections:
            if address <= rva and rva-address+length <= raw_size:
                return pointer+rva-address
        raise ValueError('RVA outside file sections')

    def string(rva):
        start = offset(rva, 1)
        end = data.find(b'\0', start, min(len(data), start+1024))
        if end < 0:
            raise ValueError('unterminated PE string')
        offset(rva, end-start+1)
        try:
            return data[start:end].decode('ascii')
        except UnicodeError as error:
            raise ValueError('non-ASCII export') from error

    directory = offset(export_rva, 40)
    base, count, name_count, functions, names, ordinals = read('<IIIIII', directory+16)
    if not 1 <= count <= 65535 or name_count > 65535 or base < 1 or base+count > 65536:
        raise ValueError('invalid export counts')
    addresses = read('<'+'I'*count, offset(functions, 4*count))
    by_index = [[] for _ in range(count)]
    name_rvas = read('<'+'I'*name_count, offset(names, 4*name_count)) if name_count else ()
    indices = read('<'+'H'*name_count, offset(ordinals, 2*name_count)) if name_count else ()
    seen = set()
    for rva, index in zip(name_rvas, indices):
        name = string(rva)
        if index >= count or not addresses[index] or not re.fullmatch(r'[A-Za-z_?][A-Za-z0-9_?@$]*', name) or name in seen:
            raise ValueError('invalid or duplicate export name')
        seen.add(name); by_index[index].append(name)
    exports = []
    for index, rva in enumerate(addresses):
        if not rva:
            continue
        entry = {'ordinal': base+index, 'names': sorted(by_index[index])}
        if export_rva <= rva < export_rva+export_size:
            entry['forwarder'] = string(rva)
        else:
            offset(rva, 1)
        exports.append(entry)
    return {'schema': 1, 'machine': 'x64', 'providerSha256': hashlib.sha256(data).hexdigest(),
            'ordinalBase': base, 'functionSlots': count, 'exports': exports}


def definition(inventory, provider):
    # Build-only trusted provider input. No runtime path override is exported.
    if not isinstance(provider, str) or not provider or any(c in provider for c in '"\r\n\0;') or provider.lower().endswith('.dll'):
        raise ValueError('invalid forwarder module (use an absolute module path without .dll)')
    if not re.match(r'^[A-Za-z]:[\\/]', provider):
        raise ValueError('forwarder provider must be absolute')
    module = '"'+provider+'"' if any(c.isspace() for c in provider) else provider
    lines = ['LIBRARY vulkan-1', 'EXPORTS']
    for item in inventory['exports']:
        ordinal = item['ordinal']
        names = item['names']
        for name in names:
            lines.append(f' {name} = {module}.{name} @{ordinal}')
        if not names:
            lines.append(f' evrm_ordinal_{ordinal} = {module}.#{ordinal} @{ordinal} NONAME')
    return '\n'.join(lines)+'\n'


def write_inventory(path, output):
    inventory = export_inventory(path)
    Path(output).write_text(json.dumps(inventory, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    return inventory
