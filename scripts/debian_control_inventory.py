"""Inventory Debian control files without extracting or executing them."""
import hashlib
import io
from pathlib import PurePosixPath
import subprocess
import tarfile


def control_inventory(package):
    """Return normalized filenames mapped to SHA256, integer mode and byte size.

    Ignore safe directories. Reject unsafe paths, non-regular files, duplicate
    filenames and archives without a regular ``control`` file.
    """
    raw = subprocess.check_output(['dpkg-deb', '--ctrl-tarfile', str(package)])
    inventory = {}
    try:
        with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as archive:
            for member in archive:
                path = PurePosixPath(member.name)
                if path.is_absolute() or '..' in path.parts:
                    raise ValueError(f'unsafe control path: {member.name}')
                if member.isdir():
                    continue
                key = path.as_posix()
                if key == '.' or not member.isfile():
                    raise ValueError(f'unsupported control entry: {member.name}')
                if key in inventory:
                    raise ValueError(f'duplicate control path: {key}')
                with archive.extractfile(member) as content:
                    data = content.read()
                inventory[key] = {
                    'sha256': hashlib.sha256(data).hexdigest(),
                    'mode': member.mode & 0o7777,
                    'size': len(data),
                }
    except tarfile.TarError as error:
        raise ValueError('invalid control archive') from error
    if 'control' not in inventory:
        raise ValueError('missing regular control file')
    return inventory
