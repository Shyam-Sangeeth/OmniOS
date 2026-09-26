#!/usr/bin/env bash
# Builds OmniOS's application launcher for the Plasma desktop.
#
#   make-desktop-launcher.sh <airootfs> <pacman.conf>
#
# It is Plasma's own launcher (Kickoff) with two changes: the OmniOS mark in the
# panel corner instead of the KDE logo, and a "Game Mode" button beside Sleep,
# Restart and Shut Down.
#
# Neither can be done by configuration. The icon could be, per user, but only
# by writing into a layout file whose applet ids are assigned at first login.
# The buttons cannot be at all: they come from a fixed model of session actions
# compiled into Plasma, with no way to add one. So this takes Kickoff's QML,
# patches it, and installs it as a separate applet, org.omnios.kickoff, which
# the default panel then uses in Kickoff's place.
#
# The source is fetched for exactly the Plasma version the image will install,
# at build time. Kickoff talks to private Plasma modules that change between
# releases, so a copy pinned in this repository would break the day Arch moves
# on — and a launcher that fails to load leaves the desktop with no menu at all.
#
# For the same reason nothing here may leave a half-made launcher behind. Every
# failure removes what was written and keeps Plasma's own, with a warning: a
# desktop with a KDE logo in the corner is fine, a desktop with no menu is not.
set -uo pipefail

readonly AIROOTFS="${1:?usage: make-desktop-launcher.sh <airootfs> <pacman.conf>}"
readonly PACCONF="${2:?usage: make-desktop-launcher.sh <airootfs> <pacman.conf>}"

readonly APPLET="$AIROOTFS/usr/share/plasma/plasmoids/org.omnios.kickoff"
# /usr/local/share, not /usr/share. That path belongs to plasma-desktop's own
# copy of this template, and Plasma looks through XDG_DATA_DIRS in order, where
# /usr/local/share comes first — so this one wins without touching a file any
# package owns.
readonly TEMPLATE="$AIROOTFS/usr/local/share/plasma/layout-templates/org.kde.plasma.desktop.defaultPanel"

rm -rf "$APPLET" "$TEMPLATE"

work="$(mktemp -d)"
pacdb="$(mktemp -d)"
trap 'rm -rf "$work" "$pacdb"' EXIT

keep_stock() {
    echo "    warning: $*" >&2
    echo "    the desktop keeps Plasma's own launcher and KDE logo" >&2
    rm -rf "$APPLET" "$TEMPLATE"
    exit 0
}

# ---- which Plasma --------------------------------------------------------
# A throwaway database, for the same reason as the baseline step in
# build-iso.sh: the question is what the image will get, not what the build
# host has.
#
# OMNIOS_PLASMA_VERSION skips the query, so the rest can be exercised on a host
# without pacman.
if [[ -n ${OMNIOS_PLASMA_VERSION:-} ]]; then
    version="$OMNIOS_PLASMA_VERSION"
else
    pacman --config "$PACCONF" --dbpath "$pacdb" -Sy --noconfirm >/dev/null 2>&1 \
        || keep_stock "could not sync a package database"
    version="$(pacman --config "$PACCONF" --dbpath "$pacdb" -Si plasma-desktop 2>/dev/null \
               | awk -F': *' '/^Version/ { print $2; exit }')"
fi
[[ -n $version ]] || keep_stock "plasma-desktop is not in the repositories"

# "6.7.5-1" is tag v6.7.5: drop the package release, and an epoch if one ever
# appears.
version="${version#*:}"
readonly TAG="v${version%-*}"
echo "    plasma-desktop $version, Kickoff from $TAG"

# ---- the source ------------------------------------------------------------
fetch() {  # <path in plasma-desktop> <destination directory>
    local url="https://invent.kde.org/plasma/plasma-desktop/-/archive/$TAG/plasma-desktop-$TAG.tar.gz?path=$1"
    local dest="$2"
    mkdir -p "$dest"
    curl -sSfL --retry 3 --connect-timeout 20 -o "$dest.tar.gz" "$url" || return 1
    tar -xzf "$dest.tar.gz" -C "$dest" || return 1
}

fetch applets/kickoff "$work/kickoff" || keep_stock "could not download Kickoff $TAG"
fetch layout-templates/org.kde.plasma.desktop.defaultPanel "$work/panel" \
    || keep_stock "could not download the default panel for $TAG"

src="$(find "$work/kickoff" -type d -path '*/applets/kickoff' | head -n 1)"
panel_src="$(find "$work/panel" -type d -name org.kde.plasma.desktop.defaultPanel | head -n 1)"
[[ -n $src && -f $src/main.qml && -f $src/LeaveButtons.qml && -f $src/main.xml && -f $src/metadata.json ]] \
    || keep_stock "Kickoff $TAG is not laid out the way this script expects"
[[ -n $panel_src && -f $panel_src/contents/layout.js && -f $panel_src/metadata.json ]] \
    || keep_stock "the default panel for $TAG is not laid out the way this script expects"

# ---- the applet, as a package rather than a compiled plugin -----------------
# Upstream compiles these files into org.kde.plasma.kickoff.so. Plasma still
# loads an applet from a plain package — QML under contents/ui, its settings
# schema under contents/config — which is what this lays out.
ui="$APPLET/contents/ui"
install -d "$ui" "$APPLET/contents/config"
cp "$src"/*.qml "$ui/"
mv "$ui/config.qml" "$APPLET/contents/config/config.qml"
# Beside the QML rather than in contents/code, because the files import it as
# "code/tools.js", relative to themselves.
install -Dm644 "$src/code/tools.js" "$ui/code/tools.js"
cp "$src/main.xml" "$APPLET/contents/config/main.xml"

# The two singletons are declared in upstream's CMakeLists, which a package
# does not have. A qmldir beside the files is the package's way of saying the
# same thing; the rest of the types are listed too so the directory's contents
# are stated rather than inferred.
{
    echo "singleton KickoffSingleton 1.0 KickoffSingleton.qml"
    echo "singleton ActionMenu 1.0 ActionMenu.qml"
    for file in "$ui"/[A-Z]*.qml; do
        name="$(basename "$file" .qml)"
        [[ $name == KickoffSingleton || $name == ActionMenu ]] && continue
        echo "$name 1.0 $name.qml"
    done
} > "$ui/qmldir"

# ---- the icon ----------------------------------------------------------------
# The mark in the panel corner is the default of the "icon" setting. Changing
# the default rather than writing a setting means anyone can still pick another
# icon in the launcher's own settings, and "Reset to default" brings this back.
sed -i 's|<default>start-here-kde-symbolic</default>|<default>omnios</default>|' \
    "$APPLET/contents/config/main.xml"
grep -q '<default>omnios</default>' "$APPLET/contents/config/main.xml" \
    || keep_stock "Kickoff $TAG no longer defaults its icon to start-here-kde-symbolic"

# ---- the Game Mode button -------------------------------------------------------
# Inserted at the front of the row that holds Sleep, Restart and Shut Down, and
# styled the same way they are. It runs omni-session-select through Plasma's
# "executable" data engine, which is how an applet starts a program.
leave="$ui/LeaveButtons.qml"
button="$work/button.qml"
cat > "$button" <<'QML'
        // OmniOS: leave the desktop for Game Mode, beside Sleep and Shut Down.
        // Added by scripts/make-desktop-launcher.sh; not part of Kickoff.
        PC3.ToolButton {
            id: omniGameModeButton
            text: i18nc("@action:button", "Game Mode")
            icon.name: "input-gaming"
            display: Plasmoid.configuration.showActionButtonCaptions ? PC3.AbstractButton.TextBesideIcon : PC3.AbstractButton.IconOnly

            PC3.ToolTip.text: i18nc("@info:tooltip", "Leave the desktop for the OmniOS game library")
            PC3.ToolTip.delay: Kirigami.Units.toolTipDelay
            PC3.ToolTip.visible: hovered

            P5Support.DataSource {
                id: omniSessionSelect
                engine: "executable"
                connectedSources: []
                onNewData: (sourceName, data) => disconnectSource(sourceName)
            }

            onClicked: {
                kickoff.expanded = false;
                omniSessionSelect.connectSource("omni-session-select game");
            }
            Keys.onEnterPressed: clicked()
            Keys.onReturnPressed: clicked()
        }

QML

# Before the Repeater inside buttonsRepeaterRow, and only there: the first
# Repeater after that id. Anything else means the file has changed shape and
# the patch would land somewhere nobody checked.
awk -v button="$button" '
    /id: buttonsRepeaterRow/ { armed = 1 }
    armed && /^[ \t]*Repeater \{/ {
        while ((getline line < button) > 0) print line
        armed = 0; done = 1
    }
    { print }
    END { exit done ? 0 : 1 }
' "$leave" > "$leave.new" || keep_stock "LeaveButtons.qml in $TAG has no button row to add Game Mode to"
mv "$leave.new" "$leave"

sed -i 's|^import org.kde.plasma.plasmoid$|import org.kde.plasma.plasmoid\nimport org.kde.plasma.plasma5support as P5Support|' "$leave"
grep -q '^import org.kde.plasma.plasma5support as P5Support$' "$leave" \
    || keep_stock "LeaveButtons.qml in $TAG does not import org.kde.plasma.plasmoid where expected"

# ---- metadata ------------------------------------------------------------------
# A new id so it cannot collide with the real Kickoff, and a name that says what
# it is in the widget list. X-Plasma-Provides stays: it is how the Meta key
# finds a launcher to open.
sed -e 's|"Id": "org.kde.plasma.kickoff"|"Id": "org.omnios.kickoff"|' \
    -e 's|"Icon": "start-here-kde"|"Icon": "omnios"|' \
    "$src/metadata.json" > "$APPLET/metadata.json"
if grep -q '"Id"' "$APPLET/metadata.json"; then
    grep -q '"Id": "org.omnios.kickoff"' "$APPLET/metadata.json" \
        || keep_stock "Kickoff $TAG metadata has an Id this script does not recognise"
else
    # Upstream leaves Id out and lets the build name the plugin. A package has
    # no build, so it has to be stated.
    sed -i '0,/"KPlugin": {/s//"KPlugin": {\n        "Id": "org.omnios.kickoff",/' "$APPLET/metadata.json"
    grep -q '"Id": "org.omnios.kickoff"' "$APPLET/metadata.json" \
        || keep_stock "could not give the launcher its own id"
fi
sed -i 's|"Name": "Application Launcher"|"Name": "OmniOS Launcher"|' "$APPLET/metadata.json"
grep -q '"KPackageStructure"' "$APPLET/metadata.json" \
    || sed -i '0,/{/s//{\n    "KPackageStructure": "Plasma\/Applet",/' "$APPLET/metadata.json"

# ---- the default panel -----------------------------------------------------------
install -d "$TEMPLATE/contents"
cp "$panel_src/metadata.json" "$TEMPLATE/metadata.json"
sed 's|panel.addWidget("org.kde.plasma.kickoff")|panel.addWidget("org.omnios.kickoff")|' \
    "$panel_src/contents/layout.js" > "$TEMPLATE/contents/layout.js"
[[ $(grep -c 'addWidget("org.omnios.kickoff")' "$TEMPLATE/contents/layout.js") -eq 1 ]] \
    || keep_stock "the default panel in $TAG does not add Kickoff the way this script expects"

# Last, because the steps above are not run under errexit: a copy that failed
# quietly must still end in Plasma's own launcher, not in a panel pointing at a
# package with a file missing.
for file in metadata.json contents/ui/main.qml contents/ui/LeaveButtons.qml \
            contents/ui/qmldir contents/ui/code/tools.js \
            contents/config/main.xml contents/config/config.qml; do
    [[ -s $APPLET/$file ]] || keep_stock "the launcher package is missing $file"
done

echo "    org.omnios.kickoff built from Kickoff $TAG"
