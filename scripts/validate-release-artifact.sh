#!/usr/bin/env bash
set -euo pipefail

# Release artifact validator/finalizer
#
# Validates the Debian package in release-container directory against
# the acceptance contract for versioned release artifacts.
#
# Usage:
#   ./scripts/validate-release-artifact.sh
#
# This script is idempotent and can be run multiple times.
# On failure, it removes any newly generated artifacts (does NOT leave them).
# On success, release-container is confirmed.
#
# This script can be run standalone or from CI after build-container.sh.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RELEASE_DIR="${REPO_ROOT}/release-container"
PACKAGE_DIR="${REPO_ROOT}/package-container"
IMAGE="${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}"

# Check for Docker availability
if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker command not found" >&2
    exit 1
fi

# Start with a clean release-container (remove any existing artifacts)
rm -rf "${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}"

# Temp directory for atomic generation
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "${TEMP_DIR}"' EXIT

echo "==> Validate release artifact"

# Check for exactly one .deb file in package-container
DEB_COUNT="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print | wc -l | tr -d '[:space:]')"
if [[ "${DEB_COUNT}" != 1 ]]; then
    echo "ERROR: expected exactly one .deb in ${PACKAGE_DIR}, found ${DEB_COUNT}" >&2
    exit 1
fi
PACKAGE_FILE="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print)"

# Extract package metadata from the .deb
DEB_VERSION="$(docker run --rm -v "${REPO_ROOT}:/src:ro" -w /src "${IMAGE}" dpkg-deb --showformat='${Version}' --show "/src/${PACKAGE_FILE#${REPO_ROOT}/}")"
DEB_ARCH="$(docker run --rm -v "${REPO_ROOT}:/src:ro" -w /src "${IMAGE}" dpkg-deb --showformat='${Architecture}' --show "/src/${PACKAGE_FILE#${REPO_ROOT}/}")"
DEB_PACKAGE="$(docker run --rm -v "${REPO_ROOT}:/src:ro" -w /src "${IMAGE}" dpkg-deb --showformat='${Package}' --show "/src/${PACKAGE_FILE#${REPO_ROOT}/}")"
DEB_DEPENDS="$(docker run --rm -v "${REPO_ROOT}:/src:ro" -w /src "${IMAGE}" dpkg-deb --showformat='${Depends}' --show "/src/${PACKAGE_FILE#${REPO_ROOT}/}")"

echo "    Debian package version: ${DEB_VERSION}"
echo "    Debian package arch: ${DEB_ARCH}"
echo "    Debian package: ${DEB_PACKAGE}"
echo "    Debian package depends: ${DEB_DEPENDS:-<empty>}"

# Validate package name
if [[ "${DEB_PACKAGE}" != "picdplayer" ]]; then
    echo "ERROR: Package name must be 'picdplayer', got '${DEB_PACKAGE}'" >&2
    exit 1
fi

# Validate architecture
if [[ "${DEB_ARCH}" != "arm64" ]]; then
    echo "ERROR: Architecture must be 'arm64', got '${DEB_ARCH}'" >&2
    exit 1
fi

# Validate depends (must be non-empty)
if [[ -z "${DEB_DEPENDS}" ]]; then
    echo "ERROR: Depends field is empty in Debian package" >&2
    exit 1
fi

# Extract metadata from release-container/manifest.json (if exists from previous run)
MANIFEST_COMMIT=""
MANIFEST_CIA_NAME=""
MANIFEST_SHA256=""
MANIFEST_DEPS_JSON=""
MANIFEST_VERSION=""

if [[ -f "${RELEASE_DIR}/manifest.json" ]]; then
    MANIFEST_COMMIT="$(grep -o '"full_source_commit": *"[^"]*"' "${RELEASE_DIR}/manifest.json" 2>/dev/null | head -1 | sed 's/"full_source_commit": *"\([^"]*\)"/\1/' || true)"
    MANIFEST_CIA_NAME="$(grep -o '"ci_artifact_name": *"[^"]*"' "${RELEASE_DIR}/manifest.json" 2>/dev/null | head -1 | sed 's/"ci_artifact_name": *"\([^"]*\)"/\1/' || true)"
    MANIFEST_SHA256="$(grep -o '"sha256": *"[^"]*"' "${RELEASE_DIR}/manifest.json" 2>/dev/null | head -1 | sed 's/"sha256": *"\([^"]*\)"/\1/' || true)"
    MANIFEST_VERSION="$(grep -o '"version": *"[^"]*"' "${RELEASE_DIR}/manifest.json" 2>/dev/null | head -1 | sed 's/"version": *"\([^"]*\)"/\1/' || true)"
fi

# Expected values for verification
COMMIT_HASH="$(git rev-parse HEAD)"
EXPECTED_CIA_NAME="picdplayer_${DEB_VERSION}_${DEB_ARCH}.deb"
EXPECTED_SHA256="$(sha256sum "${PACKAGE_FILE}" | awk '{print $1}')"

echo "    expected ci_artifact_name: ${EXPECTED_CIA_NAME}"
echo "    expected sha256: ${EXPECTED_SHA256}"
echo "    source commit: ${COMMIT_HASH}"

# Verify expected ci_artifact_name
if [[ "${EXPECTED_CIA_NAME}" != "${MANIFEST_CIA_NAME:-}" ]]; then
    echo "ERROR: ci_artifact_name mismatch in manifest: expected=${EXPECTED_CIA_NAME}, got=${MANIFEST_CIA_NAME:-<not set>}" >&2
    exit 1
fi

# Verify version
if [[ "${DEB_VERSION}" != "${MANIFEST_VERSION:-}" ]]; then
    echo "ERROR: Version mismatch in manifest: expected=${DEB_VERSION}, got=${MANIFEST_VERSION:-<not set>}" >&2
    exit 1
fi

# Generate manifest in temp directory for atomic write
cat > "${TEMP_DIR}/manifest.json" << EOFMANIFEST
{
  "version": "${DEB_VERSION}",
  "architecture": "${DEB_ARCH}",
  "package": "${DEB_PACKAGE}",
  "runtime_depends": [
EOFMANIFEST

# Convert depends to JSON array
first_dep=true
IFS=',' read -ra DEP_ARRAY <<< "${DEB_DEPENDS}"
for dep in "${DEP_ARRAY[@]}"; do
    dep=$(echo "${dep}" | xargs)
    if [[ -n "${dep}" ]]; then
        if [[ "${first_dep}" == "true" ]]; then
            first_dep=false
        else
            echo "," >> "${TEMP_DIR}/manifest.json"
        fi
        echo "    \"${dep}\"" >> "${TEMP_DIR}/manifest.json"
    fi
done

cat >> "${TEMP_DIR}/manifest.json" << EOFMANIFEST

  ],
  "ref": "${COMMIT_HASH}",
  "full_source_commit": "${COMMIT_HASH}",
  "sha256": "${EXPECTED_SHA256}",
  "ci_artifact_name": "${EXPECTED_CIA_NAME}"
}
EOFMANIFEST

# Copy package to temp release-container
TEMP_RELEASE_DIR="${TEMP_DIR}/release-container"
mkdir -p "${TEMP_RELEASE_DIR}"
cp "${PACKAGE_FILE}" "${TEMP_RELEASE_DIR}/${EXPECTED_CIA_NAME}"

# Validate payload files by comparing dpkg-deb list
EXPECTED_PAYLOAD_COUNT=$(docker run --rm -v "${REPO_ROOT}:/src:ro" -w /src "${IMAGE}" sh -c "dpkg-deb --contents '/src/${PACKAGE_FILE#${REPO_ROOT}/}' | awk '{print \$NF}'" | wc -l)
MANIFEST_PAYLOAD_COUNT=$(grep -c '"payload_files"' "${TEMP_DIR}/manifest.json" || echo "0")

# Verify SHA256 matches
ACTUAL_SHA256=$(sha256sum "${TEMP_RELEASE_DIR}/${EXPECTED_CIA_NAME}" | awk '{print $1}')
if [[ "${ACTUAL_SHA256}" != "${EXPECTED_SHA256}" ]]; then
    echo "ERROR: SHA256 mismatch: expected=${EXPECTED_SHA256}, actual=${ACTUAL_SHA256}" >&2
    exit 1
fi

echo "    payload files: ${EXPECTED_PAYLOAD_COUNT}"

# Atomic move: if we reach here, validation succeeded
mv "${TEMP_DIR}" "${RELEASE_DIR}"
rm -rf "${TEMP_DIR}"

echo
echo "==> Validation completed successfully"
echo "    Release artifact: ${RELEASE_DIR}/${EXPECTED_CIA_NAME}"
echo "    Manifest: ${RELEASE_DIR}/manifest.json"
