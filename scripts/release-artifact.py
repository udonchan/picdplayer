#!/usr/bin/env python3
"""Validate an existing CPack artifact; never build, install or publish it."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def invalidate(root):
    output = root / 'release-container'
    if output.is_symlink():
        raise ValueError('release-container must not be a symlink')
    if output.exists():
        if not output.is_dir():
            raise ValueError('release-container must be a directory')
        shutil.rmtree(output)
    return output


def stage_inventory(stage):
    if stage.is_symlink() or not stage.is_dir():
        raise ValueError('missing or symlinked stage')
    result = {}
    for directory, dirs, files in os.walk(stage, followlinks=False):
        for name in dirs + files:
            path = Path(directory) / name
            mode = path.lstat().st_mode
            key = path.relative_to(stage).as_posix()
            if stat.S_ISLNK(mode):
                result[key] = {'type': 'symlink', 'target': os.readlink(path)}
            elif stat.S_ISREG(mode):
                result[key] = {'type': 'file', 'sha256': digest(path.read_bytes()),
                               'mode': stat.S_IMODE(mode)}
            elif not stat.S_ISDIR(mode):
                raise ValueError(f'unsupported stage entry: {key}')
    if not result:
        raise ValueError('empty stage')
    return result


def package_inventory(package):
    data = subprocess.check_output(['dpkg-deb', '--fsys-tarfile', str(package)])
    result = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('unsafe package path')
            key = path.as_posix()
            if member.isdir():
                continue
            if key in result:
                raise ValueError(f'duplicate package path: {key}')
            if member.issym():
                result[key] = {'type': 'symlink', 'target': member.linkname}
            elif member.isfile():
                result[key] = {'type': 'file', 'sha256': digest(archive.extractfile(member).read()),
                               'mode': member.mode & 0o7777}
            else:
                raise ValueError(f'unsupported package entry: {key}')
    return result


def prepare(root, commit, ref, dirty=False):
    output = invalidate(root)
    try:
        if not re.fullmatch(r'[0-9a-f]{40}', commit):
            raise ValueError('expected full source commit')
        source = (root / 'CMakeLists.txt').read_text()
        match = re.search(r'project\s*\(\s*PiCDPlayer\s+VERSION\s+(\d+\.\d+\.\d+)\b', source)
        if not match:
            raise ValueError('cannot read CMake project version')
        version = match[1]
        cache = (root / 'build-container/CMakeCache.txt').read_text()
        if not re.search(r'^ENABLE_PARANOIA:BOOL=OFF$', cache, re.M):
            raise ValueError('release validation requires ENABLE_PARANOIA=OFF')
        if not re.search(r'^CMAKE_PROJECT_VERSION:STATIC=' + re.escape(version) + r'$', cache, re.M):
            raise ValueError('configured/source version mismatch')
        if ref.startswith('refs/tags/') and ref != f'refs/tags/v{version}':
            raise ValueError('tag must be vX.Y.Z and match CMake version')
        packages = list((root / 'package-container').glob('*.deb'))
        if len(packages) != 1 or packages[0].is_symlink() or not packages[0].is_file():
            raise ValueError('expected exactly one regular Debian package')
        package = packages[0]
        fields = {}
        for name in ('Package', 'Version', 'Architecture', 'Depends'):
            fields[name] = subprocess.check_output(
                ['dpkg-deb', '--field', str(package), name], text=True).strip()
        if fields['Package'] != 'picdplayer' or fields['Architecture'] != 'arm64':
            raise ValueError('unexpected package name or architecture')
        if fields['Version'] != version or not fields['Depends']:
            raise ValueError('package version mismatch or missing runtime Depends')
        payload = package_inventory(package)
        if payload != stage_inventory(root / 'stage'):
            raise ValueError('package payload does not match CMake stage')
        metadata = {'schema_version': 1, 'version': version, 'architecture': fields['Architecture'],
                    'package': fields['Package'], 'depends': fields['Depends'], 'source_commit': commit,
                    'source_ref': ref, 'source_dirty': dirty, 'artifact': package.name,
                    'sha256': digest(package.read_bytes()), 'payload': payload,
                    'validation': ['package_metadata', 'stage_payload_and_modes'],
                    'publication': 'not-approved', 'hardware_verified': False}
        with tempfile.TemporaryDirectory(prefix='.release-', dir=root) as temporary:
            pending = Path(temporary)
            shutil.copyfile(package, pending / package.name)
            (pending / 'manifest.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
            pending.rename(output)
        print(f'picdplayer-{version}-{commit}' + ('-dirty' if dirty else ''))
        return metadata
    except Exception:
        invalidate(root)
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--ref', required=True)
    parser.add_argument('--dirty', action='store_true')
    args = parser.parse_args()
    try:
        prepare(Path(__file__).resolve().parents[1], args.commit, args.ref, args.dirty)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'ERROR: {error}\n')
