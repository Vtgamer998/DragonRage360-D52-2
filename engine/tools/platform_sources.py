# SPDX-License-Identifier: AGPL-3.0-or-later
"""One reviewed source inventory shared by the native and diagnostic builds.

A new module must be deliberately added to the inventory. This does not prove
its semantics or readiness; strict compilation/link and runtime checks remain
required. Unknown modules are refused before any compiler/plugin is invoked.
"""
from pathlib import Path
import re

class PlatformSourceError(ValueError):
    pass

def reviewed_sources(project):
    project=Path(project).resolve()
    directory=project/'engine/platform/platform'
    inventory=project/'engine/platform/platform-sources.txt'
    if not inventory.is_file():raise PlatformSourceError('missing reviewed platform source inventory')
    names=inventory.read_text().splitlines()
    if not names or any(not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*\.c',n) for n in names):
        raise PlatformSourceError('invalid platform source inventory entry')
    if len(set(names))!=len(names):raise PlatformSourceError('duplicate platform source inventory entry')
    actual={f.name for f in directory.glob('*.c')}
    extra=sorted(actual-set(names));missing=sorted(set(names)-actual)
    problems=[]
    if extra:problems.append('unreviewed platform sources: '+', '.join(extra))
    if missing:problems.append('missing reviewed platform sources: '+', '.join(missing))
    # A quote include resolves in the source directory before -I directories.
    # Such a copy would silently shadow the pinned upstream compatibility shim.
    if (directory/'port_compat.h').exists():problems.append('local port_compat.h shadows pinned upstream compatibility header')
    blobs=sorted(str(f.relative_to(directory)) for f in directory.rglob('*.bin'))
    if blobs:problems.append('untyped binary resources in platform source tree: '+', '.join(blobs))
    if problems:raise PlatformSourceError('; '.join(problems))
    return [directory/n for n in names]
