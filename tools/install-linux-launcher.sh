#!/bin/sh

set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
APP_ID=io.github.tsoding.musializer
DATA_HOME=${XDG_DATA_HOME:-"$HOME/.local/share"}
BIN_HOME=${XDG_BIN_HOME:-"$HOME/.local/bin"}
DESKTOP_FILE="$DATA_HOME/applications/$APP_ID.desktop"
ICON_FILE="$DATA_HOME/icons/hicolor/256x256/apps/$APP_ID.png"
LAUNCHER_FILE="$BIN_HOME/musializer"
DESKTOP_TEMPLATE="$PROJECT_ROOT/packaging/linux/$APP_ID.desktop.in"
LAUNCHER_TEMPLATE="$PROJECT_ROOT/tools/musializer-launcher"

usage()
{
    printf '%s\n' \
        "Usage: $0 [--install|--uninstall] [--skip-build]" \
        "" \
        "Installs a per-user Linux application launcher. Installation builds the" \
        "release executable unless --skip-build is supplied."
}

mode=install
build=yes
while [ "$#" -gt 0 ]; do
    case "$1" in
        --install) mode=install ;;
        --uninstall) mode=uninstall ;;
        --skip-build) build=no ;;
        --help|-h) usage; exit 0 ;;
        *) printf '%s\n' "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

refresh_desktop_database()
{
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$DATA_HOME/applications" >/dev/null 2>&1 || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
    fi
    if command -v kbuildsycoca6 >/dev/null 2>&1; then
        kbuildsycoca6 >/dev/null 2>&1 || true
    elif command -v kbuildsycoca5 >/dev/null 2>&1; then
        kbuildsycoca5 >/dev/null 2>&1 || true
    fi
}

if [ "$mode" = uninstall ]; then
    rm -f "$DESKTOP_FILE" "$ICON_FILE" "$LAUNCHER_FILE"
    refresh_desktop_database
    printf '%s\n' "Removed the Musializer application launcher."
    exit 0
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' "This installer supports Linux desktop environments only." >&2
    exit 1
fi

if [ "$build" = yes ]; then
    if [ ! -x "$PROJECT_ROOT/nob" ]; then
        cc -o "$PROJECT_ROOT/nob" "$PROJECT_ROOT/nob.c"
    fi
    (cd "$PROJECT_ROOT" && ./nob build release)
elif [ ! -x "$PROJECT_ROOT/build/musializer" ]; then
    printf '%s\n' "--skip-build was requested, but build/musializer does not exist." >&2
    exit 1
fi

mkdir -p \
    "$DATA_HOME/applications" \
    "$DATA_HOME/icons/hicolor/256x256/apps" \
    "$BIN_HOME"

# Shell single-quote escaping is needed for the generated launcher's embedded
# project root. Desktop Exec quoting has its own escaping rules.
project_root_shell=$(printf '%s' "$PROJECT_ROOT" | sed "s/'/'\\\\''/g")
project_root_desktop=$(printf '%s' "$PROJECT_ROOT" | sed 's/[\\`$\"]/\\\\&/g')
launcher_desktop=$(printf '%s' "$LAUNCHER_FILE" | sed 's/[\\`$\"]/\\\\&/g')

temp_dir=$(mktemp -d)
launcher_tmp="$temp_dir/musializer"
desktop_tmp="$temp_dir/$APP_ID.desktop"
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

sed "s|@PROJECT_ROOT_SHELL@|'$project_root_shell'|g" \
    "$LAUNCHER_TEMPLATE" >"$launcher_tmp"
sed \
    -e "s|@LAUNCHER@|$launcher_desktop|g" \
    -e "s|@PROJECT_ROOT@|$project_root_desktop|g" \
    "$DESKTOP_TEMPLATE" >"$desktop_tmp"

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$desktop_tmp"
fi

install -m 0755 "$launcher_tmp" "$LAUNCHER_FILE"
install -m 0644 "$desktop_tmp" "$DESKTOP_FILE"
install -m 0644 "$PROJECT_ROOT/resources/logo/logo-256.png" "$ICON_FILE"
refresh_desktop_database

printf '%s\n' \
    "Musializer is installed in your application menu." \
    "Launcher: $LAUNCHER_FILE" \
    "Desktop entry: $DESKTOP_FILE" \
    "Log: ${XDG_STATE_HOME:-$HOME/.local/state}/musializer/launcher.log"
