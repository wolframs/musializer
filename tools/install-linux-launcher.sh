#!/bin/sh

set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
APP_ID=io.github.tsoding.musializer
DATA_HOME=${XDG_DATA_HOME:-"$HOME/.local/share"}
BIN_HOME=${XDG_BIN_HOME:-"$HOME/.local/bin"}
DESKTOP_FILE="$DATA_HOME/applications/$APP_ID.desktop"
ICON_FILE="$DATA_HOME/icons/hicolor/256x256/apps/$APP_ID.png"
MIME_FILE="$DATA_HOME/mime/packages/$APP_ID.xml"
LAUNCHER_FILE="$BIN_HOME/musializer"
DESKTOP_TEMPLATE="$PROJECT_ROOT/packaging/linux/$APP_ID.desktop.in"
MIME_TEMPLATE="$PROJECT_ROOT/packaging/linux/$APP_ID.xml"
LAUNCHER_TEMPLATE="$PROJECT_ROOT/tools/musializer-launcher"

usage()
{
    printf '%s\n' \
        "Usage: $0 [--install|--uninstall] [--skip-build] [--no-refresh]" \
        "" \
        "Installs a per-user Linux application launcher. Installation builds the" \
        "release executable unless --skip-build is supplied. --no-refresh skips" \
        "desktop-menu cache refresh commands (useful in containers and tests)."
}

mode=install
build=yes
refresh=yes
while [ "$#" -gt 0 ]; do
    case "$1" in
        --install) mode=install ;;
        --uninstall) mode=uninstall ;;
        --skip-build) build=no ;;
        --no-refresh) refresh=no ;;
        --help|-h) usage; exit 0 ;;
        *) printf '%s\n' "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

refresh_desktop_database()
{
    [ "$refresh" = yes ] || return 0
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$DATA_HOME/applications" >/dev/null 2>&1 || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "$DATA_HOME/mime" >/dev/null 2>&1 || true
    fi
    if command -v kbuildsycoca6 >/dev/null 2>&1; then
        kbuildsycoca6 >/dev/null 2>&1 || true
    elif command -v kbuildsycoca5 >/dev/null 2>&1; then
        kbuildsycoca5 >/dev/null 2>&1 || true
    fi
}

if [ "$mode" = uninstall ]; then
    rm -f "$DESKTOP_FILE" "$ICON_FILE" "$MIME_FILE" "$LAUNCHER_FILE"
    refresh_desktop_database
    printf '%s\n' "Removed the Musializer application launcher."
    exit 0
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' "This installer supports Linux desktop environments only." >&2
    exit 1
fi

# Source checkouts place the executable under build/. Portable Linux archives
# place it beside this tools directory and do not ship the compiler bootstrap.
app_relative=build/musializer
if [ -x "$PROJECT_ROOT/musializer" ] && [ ! -f "$PROJECT_ROOT/nob.c" ]; then
    app_relative=musializer
    build=no
fi
APP_BINARY="$PROJECT_ROOT/$app_relative"

for required_file in "$DESKTOP_TEMPLATE" "$MIME_TEMPLATE" "$LAUNCHER_TEMPLATE" \
                     "$PROJECT_ROOT/resources/logo/logo-256.png"; do
    if [ ! -f "$required_file" ]; then
        printf '%s\n' "Required launcher asset is missing: $required_file" >&2
        exit 1
    fi
done

if [ "$build" = yes ]; then
    if [ ! -x "$PROJECT_ROOT/nob" ]; then
        if ! command -v cc >/dev/null 2>&1; then
            printf '%s\n' "No C compiler named 'cc' was found in PATH." >&2
            exit 1
        fi
        cc -o "$PROJECT_ROOT/nob" "$PROJECT_ROOT/nob.c"
    fi
    (cd "$PROJECT_ROOT" && ./nob build release)
elif [ ! -x "$APP_BINARY" ]; then
    printf '%s\n' "--skip-build was requested, but $APP_BINARY does not exist." >&2
    exit 1
fi
if [ ! -x "$APP_BINARY" ]; then
    printf '%s\n' "The release build completed without producing $APP_BINARY." >&2
    exit 1
fi

mkdir -p \
    "$DATA_HOME/applications" \
    "$DATA_HOME/icons/hicolor/256x256/apps" \
    "$DATA_HOME/mime/packages" \
    "$BIN_HOME"

# Shell single-quote escaping is needed for the generated launcher's embedded
# project root. Desktop Exec quoting has its own escaping rules.
escape_sed_replacement()
{
    printf '%s' "$1" | sed 's/[\\&|]/\\&/g'
}

project_root_shell=$(printf '%s' "$PROJECT_ROOT" | sed "s/'/'\\\\''/g")
project_root_shell=$(escape_sed_replacement "'$project_root_shell'")
project_root_desktop=$(printf '%s' "$PROJECT_ROOT" | sed 's/[\\`$\"]/\\\\&/g')
project_root_desktop=$(escape_sed_replacement "$project_root_desktop")
launcher_desktop=$(printf '%s' "$LAUNCHER_FILE" | sed 's/[\\`$\"]/\\\\&/g')
launcher_desktop=$(escape_sed_replacement "$launcher_desktop")

temp_dir=$(mktemp -d)
launcher_tmp="$temp_dir/musializer"
desktop_tmp="$temp_dir/$APP_ID.desktop"
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

sed "s|@PROJECT_ROOT_SHELL@|$project_root_shell|g" \
    "$LAUNCHER_TEMPLATE" | \
    sed "s|@APP_RELATIVE@|$app_relative|g" >"$launcher_tmp"
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
install -m 0644 "$MIME_TEMPLATE" "$MIME_FILE"
refresh_desktop_database

printf '%s\n' \
    "Musializer is installed in your application menu." \
    "Launcher: $LAUNCHER_FILE" \
    "Desktop entry: $DESKTOP_FILE" \
    "Project type: $MIME_FILE" \
    "Log: ${XDG_STATE_HOME:-$HOME/.local/state}/musializer/launcher.log"
