#!/usr/bin/env bash
# Usage: tools/set_version.sh X.Y.Z
# Writes VERSION and updates the version on the last line of every README*.md.
set -euo pipefail
cd "$(dirname "$0")/.."

version="${1:?usage: $0 X.Y.Z}"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]] \
  || { echo "Invalid version: $version" >&2; exit 1; }

printf '%s\n' "$version" > VERSION
for f in README*.md; do
  # The last line is "<label>: <version>" in every language.
  sed -i -E '$ s/([:：] ?)[^ ]+[[:space:]]*$/\1'"$version"'/' "$f"
done
echo "Set version $version in VERSION and $(ls README*.md | wc -l) READMEs"
