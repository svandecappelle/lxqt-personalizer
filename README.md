# lxqt-personalizer

Create, preview, load and apply desktop themes for LXQt + Openbox.

A theme is a directory with a `theme.ini` manifest (plus optional assets)
covering:

| Backend     | Settings                                   | Files                                                         |
|-------------|--------------------------------------------|---------------------------------------------------------------|
| `lxqt`      | LXQt theme, icons, Qt style, palette, font | `~/.config/lxqt/lxqt.conf`                                    |
| `openbox`   | window decoration theme, title fonts       | `~/.config/openbox/rc.xml`                                    |
| `gtk`       | GTK theme, icons, font, dark preference    | `gtk-3.0/settings.ini`, `gtk-4.0/settings.ini`, `~/.gtkrc-2.0` |
| `cursor`    | cursor theme and size                      | `lxqt/session.conf`, `~/.icons/default`, `~/.Xresources`, GTK  |
| `wallpaper` | wallpaper file and mode                    | `~/.config/pcmanfm-qt/<profile>/settings.conf`                |
| `panel`     | position, size, colors, opacity            | `~/.config/lxqt/panel.conf`                                   |

Files are edited in place: only themed keys change, everything else
(comments, layout, unrelated settings) is kept byte for byte.

## Build

Dependencies: CMake ≥ 3.20, a C11 compiler, GLib/GIO ≥ 2.76, libxml2,
cairo, pango, gdk-pixbuf, librsvg ≥ 2.52, and GTK ≥ 4.12 for the
application (`-DLXP_BUILD_GUI=OFF` builds only the library and `lxp`).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/bin/lxqt-personalizer   # the application
```

## Application

`lxqt-personalizer` ("Desktop Themes") shows every installed theme with a
live mockup of the desktop it produces — wallpaper, Openbox window
decorations, Qt palette and fonts, icons, panel — next to the current
desktop. **Try** applies a theme and reverts after 15 seconds unless you
keep it; **Apply** applies it; the header bar saves the current desktop as
a theme and undoes the last change.

Changes are made by running `lxp` in a separate process, so a try still
reverts if the application crashes or is closed.

`LXP_SCREENSHOT=shot.png lxqt-personalizer` saves a screenshot of the window
and quits.

## Usage

```sh
lxp backends                 # supported components and the files they manage
lxp capture "My Theme"       # save the current look as a theme
lxp list                     # installed themes
lxp show my-theme            # print a theme manifest
lxp preview my-theme -o shot.png   # render the mockup (--current: this desktop)
lxp apply my-theme --try 15  # apply, revert unless confirmed within 15 s
lxp apply my-theme --only openbox,cursor
lxp reload                   # make running programs reread their config
lxp snapshot [LABEL]         # back up the managed config files
lxp snapshots                # list backups
lxp rollback [ID]            # restore a backup (default: latest)
```

`apply` first checks that every referenced theme, icon set, cursor and
wallpaper is installed (`--force` skips this), then snapshots all files it
may touch, writes them, and reloads running programs: Openbox is
reconfigured, the panel is restarted through lxqt-session, pcmanfm-qt gets
the new wallpaper, and GSettings is updated for GTK apps. LXQt apps follow
`lxqt.conf` on their own. If any write fails, the snapshot is restored.

Running GTK 2/3 apps and the cursor of already-open windows only change
after a restart of those apps (or a new session).

Themes are stored in `~/.local/share/lxqt-personalizer/themes/`, snapshots
in `~/.local/state/lxqt-personalizer/snapshots/`.

## Layout

```
src/core/           liblxp: theme model, snapshots, backends (no GUI)
src/core/backends/  one file per desktop component
src/preview/        mockup renderer (cairo/pango, no GTK), icon theme lookup
src/cli/            the `lxp` command
src/gui/            the GTK 4 application
tests/              GLib tests with isolated XDG directories
```

## License

GPL-2.0-or-later
