"""Recover the package inventory and licensed voice dependency from the player ZIP."""
import hashlib
from pathlib import Path
import sys
import zipfile


ARCHIVE_SHA256 = "06545849134347cb3cef05e15d1723a96177a8000f6641970f892179d375b70e"
VOICE_SHA256 = "f20e7bcb50f1591e1c4fdf5fb16ceda876ec599c1731fd857779b977b4e585b7"


def prepare(archive, output):
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != ARCHIVE_SHA256:
            raise ValueError("Supply the original EnshroudedVRM-0.6.1.zip release asset")
    with zipfile.ZipFile(archive) as package:
        avatars = [name for name in package.namelist() if name.startswith("EnshroudedVRM/Avatars/") and name.lower().endswith(".vrm")]
        if len(avatars) != 1:
            raise ValueError("Unexpected player package inventory")
        voice = package.read("EnshroudedVRM/Runtime/OVRLipSync.dll")
    if hashlib.sha256(voice).hexdigest() != VOICE_SHA256:
        raise ValueError("Voice library does not match the licensed release input")
    template = Path(__file__).with_name("package_members.hpp.in").read_bytes()
    placeholder = b"@BUNDLED_AVATAR_MEMBER@"
    if template.count(placeholder) != 1:
        raise ValueError("Invalid package inventory template")
    inventory = template.replace(placeholder, avatars[0].encode("ascii"))
    output.mkdir(parents=True, exist_ok=True)
    (output / "standalone_package_members.hpp").write_bytes(inventory)
    (output / "OVRLipSync.dll").write_bytes(voice)


if __name__ == "__main__":
    prepare(Path(sys.argv[1]), Path(sys.argv[2]))
