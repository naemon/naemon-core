#!/bin/sh
#
# Runs once after the dev container has been created. Deliberately does not
# build anything: the workflow stays the same as on native Linux, see
# ../.vscode/README.md.

set -e

# The workspace is a bind mount from the host, so git would otherwise refuse to
# operate on it ("dubious ownership"). version.sh calls "git describe" to derive
# the naemon version, which broker modules check via pkg-config.
git config --global --add safe.directory /workspaces/naemon-core

cat <<'EOF'

Naemon dev container is ready.

Next steps:
  1. Terminal > Run Task... > initial      (only needed once)
  2. Run and Debug > Start Debugging

See .vscode/README.md for building and debugging event broker modules.

EOF
