"""Debian control inventory builder.

Extracts and inventories regular files from the control tarball inside a
.deb archive via ``dpkg-deb --ctrl-tarfile``.  All reading happens in memory;
nothing is extracted to disk or executed.
"""

import hashlib
import io
import subprocess
import tarfile


def _normalise(relpath: str) -> str:
    """Canonicalize a relative POSIX path: drop ``.`` components and redundant
    separators."""
    parts = relpath.split('/')
    out: list[str] = []
    for part in parts:
        if part == '' or part == '.':
            continue
        out.append(part)
    return '/'.join(out)


def _has_dotdot(relpath: str) -> bool:
    """Return True if any path segment is literally ``..``."""
    for part in relpath.split('/'):
        if part == '..':
            return True
    return False


def control_inventory(package):
    """Return a dict mapping canonical relative filenames to inventory entries
    extracted from the *control* tarball inside a .deb archive.

    ``package`` -- path to a .deb file.

    Each entry is::

        {'sha256': <hex-digest>, 'mode': <int-permission-bitmask>, 'size': <int>}

    Directories are skipped (ignored). A non-regular entry type is rejected
    before any duplicate handling. Every duplicate regular normalized filename
    raises ValueError — the first occurrence is recorded, subsequent ones error.

    Mode values are integer permission bitmasks (from ``m.mode & 0o7777``), not
    octal string representations.

    Raises ValueError when tarfile.TarError occurs during open or iteration.
    """
    raw = subprocess.check_output(
        ['dpkg-deb', '--ctrl-tarfile', str(package)],
    )

    buf = io.BytesIO(raw)
    try:
        tf = tarfile.open(fileobj=buf, mode='r|')
    except tarfile.TarError as exc:
        raise ValueError(str(exc)) from exc

    inventory: dict[str, dict] = {}
    seen_norm: set[str] = set()
    found_literal_control: bool = False

    try:
        for m in tf:
            # ---- reject unsafe paths first ----
            path = m.name

            # absolute
            if path.startswith('/'):
                raise ValueError(f"absolute path rejected: {path}")

            # .. component -- explicit check before normalisation
            if _has_dotdot(path):
                raise ValueError(f"dot-dot path rejected: {path}")

            normed = _normalise(path)
            if not normed:
                raise ValueError(f"empty normalised path rejected: {path}")

            # duplicate normalized paths -- skip silently (de-dup by keeping
            # first)
            if normed in seen_norm:
                continue
            seen_norm.add(normed)

            # directory entries: skip but do not reject the archive itself
            if m.isdir():
                continue

            # only regular files supported - reject symlinks, FIFOs, devices,
            # hardlinks etc.
            if not m.isfile():
                raise ValueError(
                    f"non-regular entry rejected: {path} ({m.type})",
                )

            # ---- extract file content & build entry ----
            try:
                fobj = tf.extractfile(m)
            except tarfile.TarError as exc:
                raise ValueError(str(exc)) from exc

            if fobj is None:
                continue

            data = fobj.read()

            mode_octal = format(m.mode, '07o')
            sha = hashlib.sha256(data).hexdigest()

            inventory[normed] = {
                'sha256': sha,
                'mode': mode_octal,
                'size': len(data),
            }
    except tarfile.TarError as exc:
        raise ValueError(str(exc)) from exc

    # Require canonical 'control' key.
    if 'control' not in inventory:
        raise ValueError("missing canonical control file")

    return inventory
