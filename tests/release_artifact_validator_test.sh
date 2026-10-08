#!/usr/bin/env bash
set -euo pipefail

# CTest wrapper for validate-release-artifact.sh
#
# This wrapper runs the release artifact validator and validates its exit code.
# It is designed to run from CTest in a Docker container environment.
#
# Usage: ctest -R release_artifact_validator

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}"

if [[ ! -f "${REPO_ROOT}/release-container/manifest.json" ]]; then
    echo "SKIP: No release artifact found. Run build-container.sh first."
    exit 0
fi

# Run the validator script
"${REPO_ROOT}/scripts/validate-release-artifact.sh"

echo "Release artifact validation passed"
