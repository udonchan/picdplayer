#!/usr/bin/env python3
"""Use real Debian archives without installing packages or contacting hardware."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'release_artifact', Path(__file__).resolve().parents[1] / 'scripts/release-artifact.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
COMMIT = 'a' * 40


class ReleaseArtifactTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='picdplayer release ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'CMakeLists.txt').write_text('project(PiCDPlayer VERSION 0.1.0 LANGUAGES CXX)\n')
        (self.root / 'build-container').mkdir()
        (self.root / 'build-container/CMakeCache.txt').write_text(
            'CMAKE_PROJECT_VERSION:STATIC=0.1.0\nENABLE_PARANOIA:BOOL=OFF\n')
        self.stage = self.root / 'stage'
        (self.stage / 'usr/local/bin').mkdir(parents=True)
        self.binary = self.stage / 'usr/local/bin/cdplayerd'
        self.binary.write_bytes(b'fixture executable')
        self.binary.chmod(0o755)
        (self.stage / 'usr/local/bin/player').symlink_to('cdplayerd')
        self.packages = self.root / 'package-container'
        self.packages.mkdir()
        self.package = self.packages / 'picdplayer_0.1.0_arm64.deb'
        self.make_package()
        self.output = self.root / 'release-container'
        self.output.mkdir()
        (self.output / 'old.deb').write_text('obsolete artifact')

    def make_package(self, name='picdplayer', version='0.1.0', architecture='arm64', depends='libc6'):
        with tempfile.TemporaryDirectory(dir=self.root) as temporary:
            payload = Path(temporary) / 'payload'
            shutil.copytree(self.stage, payload, symlinks=True)
            control = payload / 'DEBIAN'
            control.mkdir()
            text = (f'Package: {name}\nVersion: {version}\nArchitecture: {architecture}\n'
                    'Maintainer: PiCDPlayer test <test@example.invalid>\nDescription: test fixture\n')
            if depends:
                text += f'Depends: {depends}\n'
            (control / 'control').write_text(text)
            subprocess.run(['dpkg-deb', '--build', str(payload), str(self.package)],
                           check=True, stdout=subprocess.DEVNULL)

    def reject(self, **arguments):
        with self.assertRaises((ValueError, OSError, subprocess.CalledProcessError)):
            release.prepare(self.root, **{'commit': COMMIT, 'ref': 'refs/heads/test', **arguments})
        self.assertFalse(self.output.exists(), 'failed validation retained final artifact')

    def test_valid_artifact_and_identity(self):
        metadata = release.prepare(self.root, COMMIT, 'refs/tags/v0.1.0')
        self.assertEqual(metadata['source_commit'], COMMIT)
        self.assertFalse(metadata['source_dirty'])
        self.assertEqual(metadata['sha256'], hashlib.sha256(self.package.read_bytes()).hexdigest())
        self.assertEqual(metadata['payload']['usr/local/bin/player']['target'], 'cdplayerd')
        self.assertEqual(self.package.read_bytes(), (self.output / self.package.name).read_bytes())
        self.assertFalse((self.output / 'old.deb').exists())
        self.assertEqual(json.loads((self.output / 'manifest.json').read_text()), metadata)
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o755)
        for file in self.output.iterdir():
            self.assertEqual(file.stat().st_mode & 0o777, 0o644)
        second = release.prepare(self.root, 'b' * 40, 'refs/heads/test', dirty=True)
        self.assertNotEqual(second['source_commit'], metadata['source_commit'])
        self.assertTrue(second['source_dirty'])

    def test_tag_mismatch(self):
        self.reject(ref='refs/tags/v0.2.0')

    def test_invalid_tag(self):
        self.reject(ref='refs/tags/0.1.0')

    def test_invalid_commit(self):
        self.reject(commit='abc123')

    def test_package_version_mismatch(self):
        self.make_package(version='0.2.0')
        self.reject()

    def test_wrong_package(self):
        self.make_package(name='other')
        self.reject()

    def test_wrong_architecture(self):
        self.make_package(architecture='amd64')
        self.reject()

    def test_missing_depends(self):
        self.make_package(depends='')
        self.reject()

    def test_multiple_packages(self):
        shutil.copyfile(self.package, self.packages / 'second.deb')
        self.reject()

    def test_missing_package(self):
        self.package.unlink()
        self.reject()

    def test_symlink_package(self):
        actual = self.root / 'actual.deb'
        self.package.rename(actual)
        self.package.symlink_to(actual)
        self.reject()

    def test_corrupt_archive(self):
        self.package.write_bytes(b'not a Debian archive')
        self.reject()

    def test_content_difference(self):
        self.binary.write_bytes(b'different')
        self.reject()

    def test_mode_difference(self):
        self.binary.chmod(0o644)
        self.reject()

    def test_symlink_difference(self):
        link = self.stage / 'usr/local/bin/player'
        link.unlink()
        link.symlink_to('other')
        self.reject()

    def test_missing_stage(self):
        shutil.rmtree(self.stage)
        self.reject()

    def test_paranoia_rejected(self):
        (self.root / 'build-container/CMakeCache.txt').write_text(
            'CMAKE_PROJECT_VERSION:STATIC=0.1.0\nENABLE_PARANOIA:BOOL=ON\n')
        self.reject()

    def test_stale_configuration(self):
        (self.root / 'CMakeLists.txt').write_text('project(PiCDPlayer VERSION 0.2.0 LANGUAGES CXX)')
        self.reject()

    def test_output_symlink_does_not_delete_target(self):
        shutil.rmtree(self.output)
        outside = self.root / 'unrelated'
        outside.mkdir()
        marker = outside / 'keep'
        marker.write_text('unrelated')
        self.output.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            release.prepare(self.root, COMMIT, 'refs/heads/test')
        self.assertEqual(marker.read_text(), 'unrelated')

    def test_wrapper_invalidates_before_docker_failure(self):
        scripts = self.root / 'scripts'
        scripts.mkdir()
        source = Path(__file__).resolve().parents[1] / 'scripts/prepare-release.sh'
        shutil.copyfile(source, scripts / source.name)
        commands = self.root / 'bin'
        commands.mkdir()
        for name, body in {
            'git': 'case "$1" in rev-parse) printf "%s\\n" ' + COMMIT + ';; '
                   'symbolic-ref) printf "refs/heads/test\\n";; status) :;; esac\n',
            'docker': 'printf "%s\\n" "$@" > "$TEST_DOCKER_ARGS"\nexit 7\n',
        }.items():
            command = commands / name
            command.write_text('#!/bin/sh\n' + body)
            command.chmod(0o755)
        arguments = self.root / 'docker-arguments'
        result = subprocess.run(['bash', str(scripts / source.name)],
                                env={**os.environ, 'PATH': f'{commands}:{os.environ["PATH"]}',
                                     'TEST_DOCKER_ARGS': str(arguments)},
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertFalse(self.output.exists())
        self.assertIn('--user\n' + f'{os.getuid()}:{os.getgid()}', arguments.read_text())


if __name__ == '__main__':
    unittest.main()
