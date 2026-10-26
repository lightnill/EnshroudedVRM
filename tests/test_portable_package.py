import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path

module_path=Path(__file__).resolve().parents[1]/'portable/package_portable.py'
spec=importlib.util.spec_from_file_location('portable_package',module_path)
package=importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class PackageTests(unittest.TestCase):
    def test_explicit_normal_members(self):
        for name in ['.gitignore','EnshroudedVRM.exe','Runtime/enshrouded_vrm_runtime.dll','Runtime/shaders/native_skinning.comp.spv','Compatibility/profile.json','Avatars/README.txt','LICENSE','vendor/tinygltf/LICENSE','src/tool_grip.cpp','shaders/avatar.hlsl','shaders/native_oit_limits.hlsli']:
            self.assertTrue(package.safe_archive_member(name),name)

    def test_private_diagnostic_and_traversal_rejected(self):
        for name in ['Avatars/Sample.vrm','Avatars/hidden.vrm','Sample-without-finger-mappings.vrm','model.glb','posed.bin','screenshot.png','State/settings.json','State/last-session.json','work/raw.json','.env','.git/config','../outside','a/../../outside','C:/outside','a\\outside','a//b','a/NUL.txt','README.txt:stream','Runtime/unexpected.exe']:
            self.assertFalse(package.safe_archive_member(name),name)

    def test_private_identifiers_rejected_in_both_encodings(self):
        for data in [b'path C:\\Users\\PrivateTester\\avatar',b'test PrivateTester name','PrivateTester'.encode('utf-16le')]:
            with self.assertRaises(ValueError):
                package.validate_content(data,private_tokens=['PrivateTester'])
        package.validate_content(b'Copyright (c) 2026 lightnill. Use your own VRM.')

    def test_exact_archive_members_reproducible_and_no_sibling_leak(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'README.txt').write_text('Portable candidate by lightnill\n')
            (root/'Private.vrm').write_bytes(b'not for distribution')
            mapping={'README.txt':root/'README.txt'}
            first=root/'first.zip';second=root/'second.zip'
            package.write_archive(first,'EnshroudedVRM-0.1.0-preview',mapping)
            package.write_archive(second,'EnshroudedVRM-0.1.0-preview',mapping)
            self.assertEqual(first.read_bytes(),second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(),['EnshroudedVRM-0.1.0-preview/README.txt'])
                self.assertFalse(archive.infolist()[0].flag_bits&1)

    def test_invalid_member_fails_before_archive_write(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);bad=root/'secret.vrm';bad.write_bytes(b'secret');destination=root/'never.zip'
            with self.assertRaises(ValueError):package.write_archive(destination,'Candidate',{'Avatars/secret.vrm':bad})
            self.assertFalse(destination.exists())

    def test_source_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'original.txt';source.write_text('not linked');linked=root/'link.txt'
            try:linked.symlink_to(source)
            except OSError:self.skipTest('Windows does not grant symlink creation to this account')
            with self.assertRaises(ValueError):package.write_archive(root/'never.zip','Candidate',{'README.txt':linked})

if __name__=='__main__':unittest.main()
