#!/bin/sh
#
# Copies the example event broker configuration from .devcontainer/examples
# into the build tree created by the "initial" task. Existing files are never
# overwritten, so your own changes survive a re-run.
#
# Called by the "broker modules: install config drop-ins" task, see
# ../.vscode/tasks.json.

set -e

ws=$(cd "$(dirname "$0")/.." && pwd)
examples="$ws/.devcontainer/examples"
etc="$ws/build/etc/naemon"

if [ ! -d "$etc" ]; then
    echo "$etc does not exist. Run the 'initial' task first." >&2
    exit 1
fi

mkdir -p "$etc/module-conf.d"

# "cp -n" warns about non-portable behaviour on newer coreutils and
# "--update=none" does not exist on older ones, so test explicitly.
install_example() {
    src="$1"
    dst="$2/$(basename "$src")"
    if [ -e "$dst" ]; then
        echo "keeping existing $dst"
    else
        cp "$src" "$dst"
        echo "installed $dst"
    fi
}

for f in "$examples"/module-conf.d/*.cfg; do
    install_example "$f" "$etc/module-conf.d"
done

for f in "$examples"/mod_gearman_neb.conf \
         "$examples"/mod_gearman_worker.conf \
         "$examples"/statusengine.toml; do
    install_example "$f" "$etc"
done

cat <<'EOT'

Remove the drop-in of any module you do not want to load from
build/etc/naemon/module-conf.d/ and adjust the paths inside the files if your
module sources are not below /workspaces/modules.
EOT
