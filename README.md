# foo_openlyrics parallel translations

Our goal is add fancy colored highlight similar to prototype: https://github.com/luncat8/translate-parallel-3-side

![build-and-test workflow](https://github.com/jacquesh/foo_openlyrics/actions/workflows/run_tests.yml/badge.svg)

An open-source lyrics plugin for [foobar2000](https://www.foobar2000.org/) that includes its own UI panel for displaying and sources for downloading lyrics that are not available locally. It is intended to be a replacement for LyricShowPanel3 so it is fully-featured and supports lyric searching, saving and editing directly from within foobar2000.

## Features
* Buttery-smooth lyric scrolling
* Supports retrieving lyrics from local files, ID3 tags or the internet
* Customise the font & colours to perfectly suite your layout & theme
* Supports using album art or any other image as the panel background, with optional transparency and blur
* Easily edit lyrics directly inside foobar2000 with built-in support for timestamps
* Check the saved lyrics of any track in your library (regardless of whether it is currently playing)
* Apply common edits (such as removing blank lines) in just 2 clicks
* ...and more!

## Screenshots
Fonts & colours are fully configurable
![](TODO)

The editor window
![](.github/readme/editor.jpg)

## How to install foo_openlyrics
1. Find the latest [release on Github](https://github.com/jacquesh/foo_openlyrics/releases).
2. Download the `fb2k-component` file attached to the release (don't worry about the `debug_symbols` zip file).
3. Double-click on the file you just downloaded. Assuming foobar2000 is installed, it should open up with the installation dialog. Restart foobar2000 when asked.
4. Add the "OpenLyrics Panel" to your layout.

## Why another lyrics plugin?
At the time that I started this, the most widely-used lyrics plugin was [foo_uie_lyrics3](https://www.foobar2000.org/components/view/foo_uie_lyrics3) which had several built-in sources but those had largely stopped working due to the relevant websites going down or otherwise becoming generally unavailable. The original developer seemed to be nowhere in sight though and the source for the plugin did not appear to be available anywhere online. There is an SDK for building one's own sources for foo_uie_lyrics3 but building plugins for plugins didn't really take my fancy. Other (more up-to-date) plugins did exist but were mostly distributed by people posting binaries for you to download from their Dropbox on Reddit. Running binaries published via Dropbox by random people on Reddit did not seem like the most amazing idea.

## Building on Windows

The plugin is built with MSBuild and the MSVC v143 toolset. From a Windows 10 command prompt:

```bat
build.bat
```

`build.bat` is a thin wrapper around `build.ps1`, which is the recommended entry point. Common options:

```bat
build.bat -Configuration Debug
build.bat -Platform x64
build.bat -Platform all -RunTests
build.bat -Clean -Configuration Release -Platform x64
```

Each build writes a timestamped log to `build\logs\`, for example `build-Release-x86-20261113-120000.log`. Compiler and linker diagnostics are also collected in the matching `*-errors.txt` file and printed to the console when the build fails. Warnings are collected in `*-warnings.txt`.

The build requires Visual Studio 2022 with the *Desktop development with C++* workload (for MSBuild and the v143 toolset), PowerShell 7 (`pwsh`), CMake 3.31.6 or newer, Git, and the Windows 10 SDK 10.0.19041.0. The first build downloads and compiles libcurl and nghttp2, so allow some extra time.

## Contributing
Please do log an issue or send a pull request if you have found a bug, would like a feature added. If you'd like to support the project you can also make a small donation using any of these links:

[![](.github/readme/bmc-button.png)](https://www.buymeacoffee.com/jacquesheunis)
[![Donate](https://liberapay.com/assets/widgets/donate.svg)](https://liberapay.com/jacquesheunis/donate)
