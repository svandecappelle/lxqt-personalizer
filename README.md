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

Dependencies: CMake ≥ 3.20, a C11 compiler, GLib/GIO ≥ 2.76, libxml2.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Usage

```sh
lxp backends                 # supported components and the files they manage
lxp capture "My Theme"       # save the current look as a theme
lxp list                     # installed themes
lxp show my-theme            # print a theme manifest
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
src/cli/            the `lxp` command
tests/              GLib tests with isolated XDG directories
```

## License

GPL-2.0-or-later
