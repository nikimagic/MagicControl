"""Fail-closed, audited reconciliation of locally retained release archives."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile
from datetime import datetime, timezone

ARCHIVE=re.compile(r'^MagicControl-\d+\.\d+\.\d+-(?:windows-x64-portable\.zip|linux-x64-portable\.tar\.gz)$')
LINE=re.compile(r'^([a-fA-F0-9]{64})  (.+)$')


def is_link(path):
    info=path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info,'st_file_attributes',0)&0x400)


def fingerprint(path):
    info=path.stat()
    return info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns


def digest(path):
    result=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024):result.update(chunk)
    return result.hexdigest()


def plan(releases, allowed_new=()):
    releases=Path(releases)
    if is_link(releases):raise ValueError('release directory is a link')
    root=releases.resolve(strict=True)
    sums=root/'SHA256SUMS.txt'
    if sums.exists() and is_link(sums):raise ValueError('checksum list is a link')
    original=sums.read_bytes() if sums.exists() else None
    existing={}
    if original is not None:
        for raw_line in original.decode('utf-8-sig').splitlines():
            match=LINE.fullmatch(raw_line)
            if not match or not ARCHIVE.fullmatch(match.group(2)):
                raise ValueError('malformed or unexpected checksum entry')
            value,name=match.group(1).lower(),match.group(2)
            if name in existing:raise ValueError('duplicate checksum entry: '+name)
            existing[name]=value
    allowed=set(allowed_new)
    if any(not ARCHIVE.fullmatch(name) for name in allowed):raise ValueError('invalid new archive name')
    paths={p.name:p for p in root.iterdir() if ARCHIVE.fullmatch(p.name)}
    if any(not path.is_file() or is_link(path) for path in paths.values()):
        raise ValueError('archive is not an ordinary file')
    unexpected=set(paths)-set(existing)-allowed
    if unexpected:raise ValueError('unlisted existing release archives: '+', '.join(sorted(unexpected)))
    absent_new=allowed-set(paths)
    if absent_new:raise ValueError('expected new archives are missing: '+', '.join(sorted(absent_new)))
    current={};stamps={}
    for name,path in sorted(paths.items()):
        before=fingerprint(path);value=digest(path)
        if fingerprint(path)!=before:raise ValueError('archive changed while hashing: '+name)
        if name in existing and existing[name]!=value:raise ValueError('archive hash differs: '+name)
        current[name]=value;stamps[name]=before
    result=''.join(f'{value}  {name}\n' for name,value in sorted(current.items())).encode('utf-8')
    return {'root':root,'sums':sums,'original':original,'result':result,'existing':existing,
            'current':current,'stamps':stamps,'missing':sorted(set(existing)-set(current)),
            'new':sorted(set(current)-set(existing))}


def apply(prepared, audit_directory):
    root=prepared['root'];sums=prepared['sums'];original=prepared['original']
    if (sums.read_bytes() if sums.exists() else None)!=original:
        raise ValueError('checksum list changed during reconciliation')
    if {p.name for p in root.iterdir() if ARCHIVE.fullmatch(p.name)}!=set(prepared['current']):
        raise ValueError('release archive inventory changed during reconciliation')
    for name,stamp in prepared['stamps'].items():
        path=root/name
        if is_link(path) or fingerprint(path)!=stamp:
            raise ValueError('archive changed after verification: '+name)
    if original==prepared['result'] or (original is None and not prepared['current']):return None
    audit=Path(audit_directory);audit.mkdir(parents=True,exist_ok=True)
    if is_link(audit):raise ValueError('audit directory is a link')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    evidence=audit/('checksums-before-'+stamp+'.txt')
    with evidence.open('xb') as output:
        output.write(original or b'');output.flush();os.fsync(output.fileno())
    details=audit/('checksums-reconcile-'+stamp+'.json')
    with details.open('x',encoding='utf-8',newline='\n') as output:
        json.dump({'utc':stamp,'old_sha256':hashlib.sha256(original or b'').hexdigest(),
                   'before':str(evidence),'missing':prepared['missing'],'new':prepared['new'],
                   'verified':prepared['current']},output,ensure_ascii=False,indent=2)
        output.write('\n');output.flush();os.fsync(output.fileno())
    temp_path=None
    try:
        with tempfile.NamedTemporaryFile(prefix='SHA256SUMS-',suffix='.tmp',dir=root,delete=False) as output:
            temp_path=Path(output.name);output.write(prepared['result']);output.flush();os.fsync(output.fileno())
        if temp_path.parent.resolve()!=root or not temp_path.name.startswith('SHA256SUMS-'):
            raise ValueError('temporary checksum path outside release directory')
        if (sums.read_bytes() if sums.exists() else None)!=original:
            raise ValueError('checksum list changed before replacement')
        os.replace(temp_path,sums);temp_path=None
    finally:
        if temp_path and temp_path.exists() and temp_path.parent.resolve()==root and temp_path.name.startswith('SHA256SUMS-'):
            temp_path.unlink()
    return {'before':str(evidence),'details':str(details)}
