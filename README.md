# Relayout

A small tray app that fixes text typed in the wrong keyboard layout.

You meant to type `hello` but your keyboard was on the wrong layout, so you
got gibberish. Select it, press your layout‑switch hotkey, and Relayout
replaces it with what you actually meant to type.

## Why another one of these?

Most tools like this install a global keyboard hook, which is more than the
job needs. Relayout doesn't. It just checks which layout you're on a few
times a second, and when it changes, quietly rewrites whatever you had
selected.

No hooks. No clipboard tricks. Just the small amount of Win32 that does the
job.

## Install

Grab `relayout.exe` from the [latest release](../../releases/latest) and run
it. That's it. No installer, no dependencies, no setup.

To start it with Windows, drop a shortcut in `shell:startup`.

## Using it

A blue icon appears in the tray. From there:

- **Switch keyboard layout** — any selected text gets fixed automatically.
- **Press Pause/Break** — fix the selection on demand, without switching
  layouts first.
- **Right‑click the tray icon** — turn auto‑fix on or off, fix now, or quit.

If you launch `relayout.exe` again while it's already running, the tray menu
pops up instead of a second copy starting.

## What it works in

Notepad, WordPad, most text fields in browsers and Electron apps, UWP apps,
File Explorer. Basically anywhere Windows' own accessibility layer can read
the selection.

It won't work in classic consoles, Windows Terminal, or a few older apps
that don't expose their text. Nothing bad happens in those — it just stays
out of the way.

## Supported layouts

Anything Windows supports. The mappings come from the system itself, so if
the layout is installed, it works.
