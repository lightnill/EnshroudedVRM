"""Allowlisted portable/source candidate archives. Never walks the workspace."""
from pathlib import Path
import hashlib
import os
import re
import stat
import tempfile
import zipfile

def safe_archive_member(name):
    if not isinstance(name,str) or not name or len(name)>240 or any(c in name for c in '\\:\x00'):
        return False
    parts=name.split('/')
    for part in parts:
        if not part or part in ('.','..') or part[-1] in '. ' or any(ord(c)<32 or c in '<>"|?*' for c in part):
            return False
        if re.fullmatch(r'(con|prn|aux|nul|com[1-9]|lpt[1-9])',part.split('.')[0],re.IGNORECASE):
            return False
    lower=name.lower()
    if any(part.lower() in ('work','state','evidence','capture','captures','.git','.codex','__pycache__') or part.lower().startswith('capture-') for part in parts):
        return False
    if parts[0].lower()=='avatars' and lower!='avatars/readme.txt':
        return False
    if parts[-1].lower() in ('.env','settings.json','last-session.json'):
        return False
    suffix=Path(name).suffix.lower()
    if suffix=='.cmd':return name in ('Launch-EnshroudedVRM.cmd','portable/script/Launch-EnshroudedVRM.cmd')
    if suffix=='.exe':return name=='EnshroudedVRM.exe'
    if suffix=='.dll':return name=='Runtime/enshrouded_vrm_runtime.dll'
    return parts[-1]=='LICENSE' or name=='.gitignore' or suffix in ('.txt','.md','.json','.spv','.hlsl','.hlsli','.cpp','.hpp','.h','.inl','.py','.ps1','.psm1','.rc','.manifest')

def validate_content(data, private_tokens=()):
    lowered=data.lower()
    for token in private_tokens:
        if not isinstance(token,str) or len(token)<4:
            raise ValueError('Privacy audit tokens must be nonempty identifiers')
        for value in {token,token.lower(),token.upper()}:
            if value.encode('utf-8').lower() in lowered or value.encode('utf-16le').lower() in lowered:
                raise ValueError('A private development identifier was found in package contents')

def write_archive(destination, root_name, members, private_tokens=()):
    destination=Path(destination)
    if '/' in root_name or not safe_archive_member(root_name+'.txt'):
        raise ValueError('Unsafe archive root')
    if not members or len(members)>4096:
        raise ValueError('Archive must have a bounded explicit file list')
    names=set();payload=[];receipt=[]
    for name,path in sorted(members.items()):
        if not safe_archive_member(name) or name.casefold() in names:
            raise ValueError('Forbidden or duplicate archive member: '+name)
        names.add(name.casefold());path=Path(path)
        info=path.lstat()
        if path.is_symlink() or not stat.S_ISREG(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise ValueError('Only regular, unlinked source files may enter an archive')
        if info.st_size>128*1024*1024:
            raise ValueError('Archive member exceeds the release file limit')
        data=path.read_bytes();validate_content(data,private_tokens)
        payload.append((root_name+'/'+name,data))
        receipt.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
    destination.parent.mkdir(parents=True,exist_ok=True)
    handle,temp=tempfile.mkstemp(prefix=destination.name+'.',suffix='.tmp',dir=destination.parent)
    os.close(handle)
    try:
        with zipfile.ZipFile(temp,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
            for name,data in payload:
                entry=zipfile.ZipInfo(name,(2026,10,26,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED;entry.external_attr=0o100644<<16
                archive.writestr(entry,data)
        with zipfile.ZipFile(temp) as archive:
            if archive.namelist()!=[name for name,_ in payload] or archive.testzip() is not None:
                raise ValueError('Archive content verification failed')
        os.replace(temp,destination)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return receipt
