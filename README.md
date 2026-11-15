# lxqt-personalizer

[![CI](https://github.com/svandecappelle/lxqt-personalizer/actions/workflows/ci.yml/badge.svg)](https://github.com/svandecappelle/lxqt-personalizer/actions/workflows/ci.yml)

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
| `kvantum`   | Kvantum theme (with the kvantum style)     | `~/.config/Kvantum/kvantum.kvconfig`                          |
| `terminal`  | QTerminal color scheme                     | `~/.config/qterminal.org/qterminal.ini`                       |

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

## Built-in themes

| Theme | Look |
|---|---|
| **Mac-like** | Light (with a dark version), frosted top bar with flat task buttons, colored window buttons on the left, centered titles, no borders, 12 px window corners with deep soft shadows, rounded 6 px widgets, smooth animations, soft color-field wallpaper. |
| **Win11-like** | Light (with a dark version), frosted 46 px taskbar with underlined active buttons, minimal window buttons with a red close on hover, left titles, 8 px window corners, 4 px widgets, subtle animations, wave wallpaper. |
| **Nord** | Dark blue-grey (with a light version), frost accent, minimal window buttons, slim top panel with pill task buttons, 8 px corners, wave wallpaper. |
| **Gruvbox** | Dark warm brown (with a light version), yellow accent, classic square windows with a 2 px border, bottom panel, no effects, geometric wallpaper. |
| **Catppuccin** | Dark indigo (with a light version), mauve accent, colored window buttons, 8 px widgets, 12 px corners, translucent top panel, blob wallpaper. |
| **Solarized** | Light cream (with a dark version), blue accent, gradient title bars, bottom panel, gradient wallpaper. |
| **Dracula** | Dark charcoal (with a light version), purple accent, colored window buttons on the left, flat bottom panel, wave wallpaper. |
| **High Contrast** | White on black, cyan accent and title bars, 3 px borders, larger text and panel, square corners, no transparency, shadows or animations. |

The palette themes also set the 16 terminal colors of their palette. Their
other version is derived by flipping the colors, so it is not the palette's
official light or dark flavor.

Built-ins are installed read-only in `/usr/share/lxqt-personalizer/themes`
and generated into `~/.cache/lxqt-personalizer` on first use. **Duplicate &
Edit…** copies one into your themes to change it. Built-ins keep your icon
and cursor themes. LXQt cannot center task buttons inside a full-width panel,
so the Win11-like taskbar keeps them on the left.

## Making themes look different

- **Generated wallpapers** — gradient, blobs, waves or geometric, drawn from
  the theme's colors (`wallpaper=` and `wallpaper_seed=` in `[Scheme]`, or
  *Desktop › Source* and *New variation* in the editor).
- **Window style** — title bar flat or gradient, title height, border,
  title alignment and buttons: Openbox classic, *minimal* (thin glyphs, red
  close on hover) or *colored dots*, with any title button layout
  (`CIML` puts close, minimize and maximize on the left).
- **Effects** — picom blur behind translucent panels and windows, and
  animation presets (subtle, smooth, dynamic; picom 12 or newer).
- **Palette tools** — colors from the wallpaper, light ⇄ dark, opposite or
  nearby accent.
- **Rounded Qt widgets** — with the *kvantum* widget style, a Kvantum theme
  is generated from the colors: rounded buttons, fields, menus, selections,
  tabs, scrollbars, sliders, check boxes and radio buttons, with one corner
  radius (`widget_radius=` in `[Scheme]`, *Widget corners* in the editor).
  It is installed into `~/.config/Kvantum`; applications pick it up when
  restarted.
- **GTK corners** — the same radius on GTK buttons, fields and popovers.
- **Panel styles** — task buttons as an underline, pills or flat
  (`panel_style=`), with the generated LXQt theme.
- **Terminal colors** — a matching QTerminal color scheme for every
  generated theme (`terminal` backend; new terminal windows use it).
  Any of the 16 ANSI colors can be picked in the editor's *Terminal*
  section (`ansi0=`…`ansi15=`, 8-15 the bright ones); the rest are derived.
  *Keep current colors* leaves `terminal` out of `generate=`: the theme
  then keeps the installed scheme it names, or leaves the terminal alone.
- **Light and dark in one theme** — `variant=true` also generates the
  opposite version (*Also make a dark/light version* in the editor); the
  app shows a Light/Dark switch, and `lxp apply|preview --variant` uses it.
- **Light by day, dark by night** — `lxp auto on THEME` switches a theme
  with both versions on a schedule (see below).
- **Accent folders** — `folder_color=true` recolors the folder icons of an
  SVG icon theme (Breeze, Papirus…) to the accent, as a small generated icon
  theme that inherits the original.
- **Surprise me** — a random, harmonious design in one click, or
  `lxp new --surprise [NAME]`.

## Light by day, dark by night

```sh
lxp auto on nord --light 07:30 --dark 19:00   # defaults: 07:00 and 19:00
lxp auto status
lxp auto off
```

`auto on` needs a theme with both versions (`variant=true`; every built-in
except High Contrast). It applies the version due now, saves the schedule
in `~/.config/lxqt-personalizer/auto.conf` and starts a systemd user timer
(`~/.config/systemd/user/lxp-auto.timer`) that runs `lxp auto run` at both
times, and at login when a switch was missed while the computer was off.

`auto run` does nothing when the version due was the last one it applied,
so a theme you apply by hand stays until the next switch (`--force` applies
anyway). Without systemd, `lxp auto on --no-timer` only saves the schedule:
run `lxp auto run` from cron or at login. Each switch takes a snapshot like
any apply, and only the 20 most recent snapshots are kept.

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
packs (the menu also exports without bundled themes; generated Kvantum
themes are part of the theme). **Apply Parts…**
applies or tries only some parts of a theme, **History** lists the backups
to restore, and the details warn about anything a theme needs that is
missing. **New Theme** (Ctrl+N) and **Edit Colors…** open the editor, with a
live preview:

- **Colors** — name, author, description, presets, the scheme colors
  (optional text-on-accent, title bar, panel and link colors; Openbox menu
  background and highlight under Appearance → Windows), contrast
  warnings;
- **Appearance** — window (Openbox) theme, title buttons, title font,
  border when maximized, minimize animation; GTK and LXQt themes, Qt widget
  style and Kvantum theme, icons, font, toolbar button style, icons
  following the colors; GTK icons, font and light/dark variant; cursor
  theme and size;
- **Desktop** — wallpaper; panel position, size, opacity, icon size, rows,
  length, alignment, auto-hide and reserved space;
- **Effects** — corner radius, shadows (strength, blur, offsets), inactive
  window opacity, fading (picom).

With an installed LXQt theme, the mockup draws the panel from that theme's
`lxqt-panel.qss`: background (plain, translucent or gradient), text color
and the task buttons' fill and border. Images and per-plugin styling are not
drawn, so it is an approximation.

The window, GTK and LXQt themes are either *Generated from colors* or an
installed theme; the choice is stored as `generate=` in `[Scheme]`, so
`lxp generate` keeps installed choices. Fusion is the widget style that
follows the colors everywhere; others (kvantum, Breeze…) use their own.

Changes are made by running `lxp` in a separate process, so a try still
reverts if the application crashes or is closed.

`LXP_SCREENSHOT=shot.png lxqt-personalizer` saves a screenshot of the window
and quits; add `LXP_SCREENSHOT_ACTION=win.new-theme` to capture the editor.

Rounded window corners and shadows come from the picom compositor, not
from the Openbox theme: Openbox draws square frames. The `compositor`
backend edits only the top-level settings it themes in `picom.conf`
(`corner-radius`, `shadow*`, `fading`, `inactive-opacity`), keeps comments
and exclusion lists, makes picom reload with `SIGUSR1` (only when the file
changed since the last reload: a reload stops and restarts compositing, which
makes docks and panels recreate their windows), checks that picom
actually starts, and warns when your configuration expects picom but it is
not running. The preview draws the corners and shadows.

With blur on, picom would blur behind the whole rectangle of every window
that has transparency, so a dock with a wide transparent window (Plank and
similar) turns into a blurred band across the screen. The backend therefore
writes a `blur-background-exclude` list: blur stays behind ordinary windows
and the LXQt panel, and is off for other docks, the desktop, menus,
tooltips and the shadow margins of GTK windows. A list you wrote yourself,
or a picom `rules` setting, is left alone.

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
lxp auto on my-theme --light 07:30 --dark 19:00   # light by day, dark by night
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
data/               built-in themes, desktop entry, AppStream metainfo, icon, screenshots
.github/workflows/  CI: build, tests and metadata validation in an Arch Linux container
```

## License

GPL-2.0-or-later
