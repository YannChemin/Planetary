# Tell GRASS GIS to search the Planetary system-wide addon tree.
# GRASS_ADDON_PATH is already written to /etc/environment by postinst so it
# is available in all PAM sessions (graphical, non-login shells, launchers).
# This profile.d is a belt-and-suspenders fallback for login shells only.
if [ -d /usr/lib/grass/addons ]; then
    _pa=/usr/lib/grass/addons/bin:/usr/lib/grass/addons/scripts
    case ":${GRASS_ADDON_PATH}:" in
        *":$_pa:"*|*":${_pa%:*}:"*)
            : ;;  # already present — skip
        *)
            export GRASS_ADDON_PATH=${GRASS_ADDON_PATH:+$GRASS_ADDON_PATH:}$_pa
            ;;
    esac
    unset _pa
fi
