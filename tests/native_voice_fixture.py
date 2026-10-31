"""Stage only the hash-pinned official local SDK beside a hidden test binary."""
from pathlib import Path
import hashlib
import os

SDK_SHA256='f20e7bcb50f1591e1c4fdf5fb16ceda876ec599c1731fd857779b977b4e585b7'
def stage_voice_library(folder):
    source=Path(__file__).resolve().parents[1]/'vendor/oculus-lipsync/Win64/OVRLipSync.dll'
    if hashlib.sha256(source.read_bytes()).hexdigest()!=SDK_SHA256:
        raise ValueError('Official SDK pin mismatch')
    destination=Path(folder)/'OVRLipSync.dll'
    if destination.exists():
        if hashlib.sha256(destination.read_bytes()).hexdigest()!=SDK_SHA256:raise ValueError('Existing fixture dependency differs')
        return source
    try:os.link(source,destination)
    except OSError:
        with destination.open('xb') as output:output.write(source.read_bytes())
    return source
