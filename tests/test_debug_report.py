"""Owned CPU report fixtures; no real hardware, game, DLL, GPU or uploads."""
from pathlib import Path
import binascii,json,os,shutil,struct,subprocess,tempfile,unittest,zipfile,zlib

class DebugReportTests(unittest.TestCase):
    def test_gpu_contract(self):
        root=Path(__file__).resolve().parents[1];compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not compiler or not headers:self.skipTest('Native compiler and Vulkan headers required')
        self.assertTrue((root/'src/debug_report_gpu.hpp').exists(),'Bounded screenshot GPU lifecycle is missing')
        with tempfile.TemporaryDirectory(prefix='evrm-report-gpu-',ignore_cleanup_errors=True) as directory:
            binary=Path(directory)/'report-gpu.exe'
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+headers,'-I'+str(root/'vendor/imgui'),
                str(root/'tests/debug_report_gpu.cpp'),str(root/'src/debug_report_gpu.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())

    def test_menu(self):
        root=Path(__file__).resolve().parents[1];compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:self.skipTest('Native compiler required')
        self.assertIn('report_create',(root/'src/mod_menu.hpp').read_text(),'F8 report controls are missing')
        with tempfile.TemporaryDirectory(prefix='evrm-report-menu-',ignore_cleanup_errors=True) as directory:
            binary=Path(directory)/'report-menu.exe';imgui=root/'vendor/imgui'
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/report_menu.cpp'),str(root/'src/mod_menu.cpp'),
                *[str(imgui/name) for name in ('imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp')],'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())

    def test_service(self):
        root=Path(__file__).resolve().parents[1]
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not compiler or not headers:self.skipTest('Native compiler and Vulkan headers required')
        self.assertTrue((root/'src/debug_report_service.cpp').exists(),'Report worker and approval/file lifecycle are not implemented')
        with tempfile.TemporaryDirectory(prefix='evrm-report-service-',ignore_cleanup_errors=True) as directory:
            binary=Path(directory)/'report-service.exe'
            command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-DEVRM_DEBUG_REPORT_TESTING','-I'+headers,
                str(root/'tests/debug_report_service.cpp'),*[str(root/'src'/name) for name in ('debug_report_service.cpp','debug_report_policy.cpp',
                    'debug_report_archive.cpp','debug_report_sources.cpp','standalone_files.cpp')],'-ladvapi32','-lbcrypt','-lshell32','-lole32','-luuid','-o',str(binary)]
            build=subprocess.run(command,capture_output=True,text=True,timeout=180)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            result=subprocess.run([str(binary),directory],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            archives=list((Path(directory)/'Reports').glob('*.zip'));self.assertEqual(len(archives),3,[p.name for p in (Path(directory)/'Reports').iterdir()])
            images=0
            for filename in archives:
                with zipfile.ZipFile(filename) as archive:
                    self.assertIsNone(archive.testzip());report=json.loads(archive.read('report.json'))
                    trace=archive.read('diagnostics.log').decode('utf-8')
                    self.assertIn('preview-ownership.sample stage=1 path-mask=1',trace)
                    self.assertIn('preview-handoff.sample stage=5 origin=1',trace)
                    self.assertIn('group-0=27395',trace)
                    self.assertNotIn('12648430',trace);self.assertNotIn('305419896',trace)
                    self.assertEqual(report['screenshot_included'],'screenshot.png' in archive.namelist())
                    images+=report['screenshot_included']
                    for name in archive.namelist():self.assertNotIn(b'SECRET_CANARY',archive.read(name))
            self.assertEqual(images,1)
            for folder in ('Retired','PreviewUnavailable'):
                for filename in (Path(directory)/folder).glob('*.zip'):
                    with zipfile.ZipFile(filename) as archive:self.assertNotIn('screenshot.png',archive.namelist())
            self.assertFalse(list(Path(directory).rglob('*.pending')))
            print(result.stdout.strip())

    def test_sources(self):
        root=Path(__file__).resolve().parents[1]
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not compiler or not headers:self.skipTest('Native compiler and Vulkan headers required')
        self.assertTrue((root/'src/debug_report_sources.cpp').exists(),'Read-only report sources are not implemented')
        with tempfile.TemporaryDirectory(prefix='evrm-report-sources-',ignore_cleanup_errors=True) as directory:
            binary=Path(directory)/'report-sources.exe'
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+headers,
                str(root/'tests/debug_report_sources.cpp'),str(root/'src/debug_report_sources.cpp'),str(root/'src/debug_report_policy.cpp'),
                str(root/'src/native_body_coverage.cpp'),'-ladvapi32','-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())

    def test_policy_archive(self):
        root=Path(__file__).resolve().parents[1]
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:self.skipTest('Native compiler required')
        required=('src/debug_report_policy.cpp','src/debug_report_archive.cpp','src/debug_report_log.hpp')
        self.assertTrue(all((root/name).is_file() for name in required),'Safe debug report exporter is not implemented')
        with tempfile.TemporaryDirectory(prefix='evrm-report-',ignore_cleanup_errors=True) as directory:
            binary=Path(directory)/'report-policy.exe'
            command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/debug_report_policy.cpp'),
                     str(root/'src/debug_report_policy.cpp'),str(root/'src/debug_report_archive.cpp'),'-o',str(binary)]
            build=subprocess.run(command,capture_output=True,text=True,timeout=180)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            result=subprocess.run([str(binary),directory],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            with zipfile.ZipFile(Path(directory)/'report.zip') as archive:
                self.assertEqual(set(archive.namelist()),{'report.json','system.json','settings.json','model.json','scene.json','diagnostics.log','README.txt'})
                self.assertIsNone(archive.testzip())
                payload=b'\n'.join(archive.read(name) for name in archive.namelist())
                for canary in (b'SECRET_CANARY',b'76561198012345678',b'alice@example.com',b'203.0.113.77',b'C:\\Users\\Alice'):
                    self.assertNotIn(canary,payload)
                scene=json.loads(archive.read('scene.json'))
                self.assertTrue(scene['map_location']['available'])
                self.assertEqual(scene['map_location']['xyz'],[3507.75,1047.0,1981.75])
                self.assertEqual(scene['map_location']['source'],'enshrouded-local-world-player')
                settings=json.loads(archive.read('settings.json'))
                self.assertEqual(settings['graphics'],{'fogQuality':3,'shadowQuality':2})
                self.assertNotIn('microphone',settings)
            png=(Path(directory)/'sample.png').read_bytes()
            self.assertEqual(png[:8],b'\x89PNG\r\n\x1a\n')
            chunks=[];offset=8
            while offset<len(png):
                size=struct.unpack_from('>I',png,offset)[0];kind=png[offset+4:offset+8];data=png[offset+8:offset+8+size]
                crc=struct.unpack_from('>I',png,offset+8+size)[0]
                self.assertEqual(crc,binascii.crc32(kind+data)&0xffffffff)
                chunks.append((kind,data));offset+=size+12
            self.assertEqual([kind for kind,_ in chunks],[b'IHDR',b'IDAT',b'IEND'])
            self.assertEqual(struct.unpack('>IIBBBBB',chunks[0][1]),(2,2,8,6,0,0,0))
            self.assertEqual(zlib.decompress(chunks[1][1]),b'\0\xff\0\0\xff\0\xff\0\xff\0\0\0\xff\xff\xff\xff\xff\xff')
            print(result.stdout.strip())

if __name__=='__main__':unittest.main()
