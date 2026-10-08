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
| `compositor`| rounded corners, shadows, fading (picom)   | `~/.config/picom.conf`                                        |

Files are edited in place: only themed keys change, everything else
(comments, layout, unrelated settings) is kept byte for byte.

## Build

Dependencies: CMake ≥ 3.20, a C11 compiler, GLib/GIO ≥ 2.76, libxml2,
cairo, pango, gdk-pixbuf, librsvg ≥ 2.52, and GTK ≥ 4.12 for the
application (`-DLXP_BUILD_GUI=OFF` builds only the library and `lxp`).
Recoloring GTK themes uses `gresource` (part of GLib) at run time.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/bin/lxqt-personalizer   # the application
```

## Themes from a few colors

A theme with a `[Scheme]` section is generated from four colors —
background, surface (lists and fields), text and accent — plus optional
accent text, title bar and panel colors and a font:

```sh
lxp new "Nord Night" --background '#2e3440' --surface '#3b4252' \
        --foreground '#eceff4' --accent '#88c0d0' --font 'Noto Sans 10' \
        --wallpaper ~/Pictures/mountains.jpg
lxp generate nord-night      # after editing [Scheme] in theme.ini
```

This writes, inside the theme folder's `data/`:

- an Openbox theme (`themes/lxp-<name>/openbox-3/themerc`);
- GTK 3 and GTK 4 themes, made by recoloring the stylesheets of the
  installed GTK so every widget follows the palette;
- an LXQt theme (panel, runner and notification stylesheets);

and sets the Qt palette, fonts and theme names in `theme.ini`. Applying the
theme installs `data/` into `~/.local/share`, never over folders it did not
create (they carry a `.lxp-asset` marker). Themes stay self-contained: the
wallpaper is copied into the theme folder.

## Sharing themes

```sh
lxp export my-theme -o my-theme.tar.gz   # theme.ini, wallpaper, data/, preview.png
lxp import my-theme.tar.gz               # --force replaces an installed theme
lxp check my-theme                       # what it needs that is missing
```

A pack bundles the Openbox, GTK and LXQt themes it uses when they are
installed in your home folder (system themes come from packages). On import
they are installed only if nothing with that name exists yet.

Packs are treated as untrusted: only regular files and directories are
extracted, absolute paths and `..` are refused, sizes are limited, bundled
data may only go to `themes`, `lxqt/themes` and `icons`, and values written
to configuration files cannot span lines.

## Application

`lxqt-personalizer` ("Desktop Themes") shows every installed theme with a
live mockup of the desktop it produces — wallpaper, Openbox window
decorations, Qt palette and fonts, icons, panel — next to the current
desktop. **Try** applies a theme and reverts after 15 seconds unless you
keep it; **Apply** applies it; the header bar creates a new theme, saves
the current desktop as a theme and undoes the last change.

**Import Theme…** (Ctrl+O, in the menu) and **Export…** share themes as
packs. **New Theme** (Ctrl+N) and **Edit Colors…** open the editor: presets,
color pickers, font, icons, wallpaper and panel options, contrast warnings
and a live preview of the generated theme.

Changes are made by running `lxp` in a separate process, so a try still
reverts if the application crashes or is closed.

`LXP_SCREENSHOT=shot.png lxqt-personalizer` saves a screenshot of the window
and quits; add `LXP_SCREENSHOT_ACTION=win.new-theme` to capture the editor.

Rounded window corners and shadows come from the picom compositor, not
from the Openbox theme: Openbox draws square frames. The `compositor`
backend edits only the top-level settings it themes in `picom.conf`
(`corner-radius`, `shadow*`, `fading`, `inactive-opacity`), keeps comments
and exclusion lists, makes picom reload with `SIGUSR1`, checks that picom
actually starts, and warns when your configuration expects picom but it is
not running. The preview draws the corners and shadows.

## Usage

```sh
lxp backends                 # supported components and the files they manage
lxp capture "My Theme"       # save the current look as a theme
lxp list                     # installed themes
lxp show my-theme            # print a theme manifest
lxp preview my-theme -o shot.png   # render the mockup (--current: this desktop)
lxp new NAME --background … --surface … --foreground … --accent …
lxp generate my-theme        # regenerate from [Scheme]
lxp export my-theme | lxp import FILE | lxp check my-theme
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

## Arch Linux package

```sh
cd packaging/arch && makepkg -si
```

The PKGBUILD builds the committed state of this checkout and runs the test
suite; `LXP_REPO=/path/or/url makepkg -si` builds another clone.

## Layout

```
src/core/           liblxp: theme model, snapshots, backends (no GUI)
src/core/backends/  one file per desktop component
src/core/templates/ Openbox, LXQt and GTK templates for generated themes
src/preview/        mockup renderer (cairo/pango, no GTK), icon theme lookup
src/cli/            the `lxp` command
src/gui/            the GTK 4 application
tests/              GLib tests with isolated XDG directories
```

## License

GPL-2.0-or-later
