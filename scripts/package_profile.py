#!/usr/bin/env python3
"""Validate a trusted local CPack package inside the disposable build container.

This executes packaged identity CLIs, not maintainer scripts. It is not a
security scanner for untrusted packages; never run it on the host or a Pi.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


BUILD_TYPES = ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')


def validate(package, expected):
    if expected not in BUILD_TYPES:
        raise ValueError('unsupported expected build type')
    profile = 'Debug' if expected == 'Debug' else 'Release'
    version = subprocess.check_output(['dpkg-deb', '--field', str(package), 'Version'], text=True).strip()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('invalid package version')
    marker = f'build_type={expected}\nprofile={profile}\nversion={version}\n'
    with tempfile.TemporaryDirectory(prefix='picdplayer-profile-') as temporary:
        root = Path(temporary)
        subprocess.run(['dpkg-deb', '--extract', str(package), str(root)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        identity = root / 'usr/local/share/picdplayer/build-profile'
        if identity.is_symlink() or not identity.is_file() or identity.stat().st_size != len(marker):
            raise ValueError('missing or malformed package build profile')
        if identity.read_bytes() != marker.encode('ascii'):
            raise ValueError('package build profile mismatch')
        # Do not inherit credentials, loader overrides or runtime diagnostics.
        env = {'PATH': os.defpath, 'PICDPLAYER_INSPECTOR': 'off',
               'LD_LIBRARY_PATH': str(root / 'opt/picdplayer-wpe/2.54.0/lib')}
        for relative, option, output in (
                ('usr/local/bin/cdplayerd', '--build-info',
                 f'build_type={expected} profile={profile}\n'),
                ('usr/local/libexec/picdplayer-wpe', '--diagnostics',
                 f'wpe_profile: build_type={expected} profile={profile} inspector=off\n')):
            binary = root / relative
            if binary.is_symlink() or not binary.is_file():
                raise ValueError('missing regular package identity binary')
            result = subprocess.run([str(binary), option], env=env, capture_output=True,
                                    timeout=10)
            if result.returncode or result.stdout != output.encode('ascii'):
                raise ValueError('package binary build profile mismatch')
        wrapper = root / 'usr/local/libexec/picdplayer-kiosk'
        diagnostics = int(profile == 'Debug')
        expected_line = f"echo 'kiosk_profile: build_type={expected} diagnostics={diagnostics}'"
        if wrapper.is_symlink() or not wrapper.is_file() or expected_line not in wrapper.read_text().splitlines():
            raise ValueError('package wrapper build profile mismatch')
    return marker


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('expected', choices=BUILD_TYPES)
    args = parser.parse_args()
    try:
        print(validate(args.package, args.expected), end='')
    except (ValueError, OSError, subprocess.SubprocessError):
        parser.exit(1, 'ERROR: package identity validation failed; rebuild before deploying\n')
