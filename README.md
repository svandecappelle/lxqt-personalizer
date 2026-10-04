# lxqt-personalizer

Create, preview, load and apply desktop themes for LXQt + Openbox.

A theme is a directory with a `theme.ini` manifest (plus optional assets)
covering the LXQt theme, icons, Qt style and palette, and Openbox
decorations. GTK, cursor, wallpaper and panel support is planned.

## Build

Dependencies: CMake ≥ 3.20, a C11 compiler, GLib ≥ 2.76, libxml2.

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
lxp snapshot [LABEL]         # back up the managed config files
lxp snapshots                # list backups
lxp rollback [ID]            # restore a backup (default: latest)
```

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
