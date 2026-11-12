# ViewMD

A lightweight GTK 3 markdown viewer for desktop Linux. It is ideal as your default app for opening `.md` files. Uses the excellent library [md4c](https://github.com/mity/md4c) for markdown parsing. This project is sort of a companion app/fork of my other markdown project [TrayMD](https://github.com/rabfulton/TrayMD). 

![ViewMD](assets/screenshot.png)

## Features

- **Native GTK viewer** - No webviews
- **Read-only rendering** - Focused on viewing markdown files
- **Automatic reload** - Updates the preview when the open file changes on disk
- **Minimal UI** - Clean toolbar with open and settings buttons
- **Lightweight** - Pure C, no web technologies, fast startup
- **Hyperlink support** - Left click opens links and internal anchors
- **Document search** - `Ctrl+F` with next/previous match navigation
- **Local image support** - Local images are resized to fit the the document window

## Supported Markdown

| Syntax | Description |
|--------|-------------|
| `# Header 1` | Large bold header |
| `## Header 2` | Medium bold header |
| `### Header 3` | Small bold header |
| `**bold**` | Bold text |
| `*italic*` | Italic text |
| `` `code` `` | Inline code |
| <code>```...```</code> | Code block |
| `- item` | List item |
| `> quote` | Block quote |
| `[text](url)` | Link |
| `~~strike~~` | Strikethrough |
| `\| table \| row \|` | Markdown tables |
| `---` | Horizontal rule |
| `[^note]` and `[^note]: Text` | Numbered footnotes with return links |

Code blocks currently support a beta version of keyword highlighting for fenced languages `c`, `java`, and `python`.

YAML front matter is hidden from the preview by default. To display it as ordinary
Markdown, turn off **Hide YAML front matter** in Settings and click **Apply**.
The preference is saved between sessions. A block must open with `---` on
the first line (optionally after a UTF-8 BOM) and close with `---` or `...` on
its own line. Unclosed blocks and delimiters later in the document are rendered
as ordinary Markdown. A pair of `---` delimiters at the start is interpreted as
front matter; use `***` for a leading horizontal rule when that would be ambiguous.

Footnote definitions may appear anywhere at the top level of the document.
References are numbered by first use, and the notes appear below a horizontal
rule at the end with a return link for each occurrence. Indent continuation
lines and additional paragraphs by four spaces or one tab. Names are case-sensitive
and cannot contain whitespace, brackets, or backslashes. The first definition of a name wins;
unreferenced definitions are omitted, and undefined references remain literal.
Markers in code, escaped text, links, and image alt text stay literal. Inline
definitions (`^[text]`) and references inside footnote bodies are not supported.

## Installation

### Arch Linux (AUR)

https://aur.archlinux.org/packages/viewmd

### Debian/Ubuntu (.deb)

Install a built `.deb` package with:

```bash
sudo apt install ./viewmd_*.deb
```

### Fedora/RHEL (.rpm)

Install a built `.rpm` package with:

```bash
sudo dnf install ./viewmd-*.rpm
```

## Usage

Run `viewmd` to start the application.

- **Open button**: Open a markdown document
- **Reload button**: Reload the currently open document from disk
- **Settings button**: Adjust theme, fonts, and markdown accent colors

Files reload automatically after they are saved in another editor, including
editors that replace the file when saving. ViewMD keeps your scroll position and
retains the last preview if the file is temporarily unavailable. To keep a static
preview, disable **Automatically reload changed files** in Settings and click
**Apply**. The preference is saved between sessions; the Reload button and F5
remain available for manual updates.

### Find in Document

- Press `Ctrl+F` to open search.
- Type to highlight matches as you search.
- Press `Enter` for next match and `Shift+Enter` for previous match.
- Press `Esc` to close search.

### Set as Default `.md` Viewer

After installing, associate markdown MIME types with `viewmd.desktop`:

```bash
xdg-mime default viewmd.desktop text/markdown
xdg-mime default viewmd.desktop text/x-markdown
```

Verify the current default:

```bash
xdg-mime query default text/markdown
```

Test by opening a markdown file through your desktop association:

```bash
xdg-open README.md
```


## Building From Source

```bash
make
sudo make install
```

This installs:
- Binary to `/usr/local/bin/viewmd`
- Desktop file to `/usr/local/share/applications/viewmd.desktop`

### Uninstallation

```bash
sudo make uninstall
```
### Dependencies

### Arch Linux
```bash
sudo pacman -S gtk3
```

### Ubuntu/Debian
```bash
sudo apt install libgtk-3-dev
```

### Fedora
```bash
sudo dnf install gtk3-devel
```

## Packaging Templates

- Arch: `packaging/arch/PKGBUILD`
- Debian: `packaging/deb/control.in`
- RPM: `packaging/rpm/viewmd.spec.in`
## License

MIT License

## Other Useful Projects
- TrayMD is an app for taking notes in markdown with live editing [TrayMD](https://github.com/rabfulton/TrayMD)
- Preditor is an image viewer with a similar philosophy to viewmd [preditor](https://github.com/rabfulton/preditor)
- Try my AI panel plugin for XFCE [XFCE Ask](https://github.com/rabfulton/xfce-ask)
- For a feature complete AI application try out [ChatGTK](https://github.com/rabfulton/ChatGTK)
- A lightweight speech to text implementation [Auriscribe](https://github.com/rabfulton/Auriscribe)
- A lightweight local movie database and browser [ReelVault](https://github.com/rabfulton/ReelVault)
