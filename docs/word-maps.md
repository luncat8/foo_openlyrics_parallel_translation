# Parallel-translation word maps

OpenLyrics can display a lyric bundle that carries, for each line, the original text plus
one or more translations stacked underneath, coloured by part of speech, with a cursor that
follows playback through all languages at once. This document describes what the component
reads, how it is drawn, and what each preference does.

The file format is defined by the [LinguaMap](https://github.com/luncat8/translate-parallel-3-side)
project. This component implements a *reader* for it; it does not write bundles.

## Where a bundle comes from

1. **A sidecar file.** If OpenLyrics saves lyrics for a track as
   `Artist - Title.lrc`, then `Artist - Title.lrc.lmz.txt` in the same directory is read as
   a bundle for that track. This is the normal case: the bundle sits next to the lyrics it
   belongs to, and nothing else in the directory has to change.
2. **Pasted text.** A bundle pasted into the lyric editor, or stored in a lyrics tag, is
   recognised by its first line and parsed the same way.

A track with no bundle renders exactly as it did before this feature existed: the plain
renderer is bypassed, never modified.

## The wire format

The normative description is the header comment in `src/lyric_map.cpp`; the prose version is
the LinguaMap repository's own documentation. Summary of the parts a user will actually
write:

```
#!linguamap 1
@title        Jumpin' Girl
@source       jp
@translit     tr
@columns      jp,tr,ru,en
@columnTitles ru=Русский,en=English
@posDefault   part
@timeFormat   lrc

[00:12.40]
{夕暮れ:q} {空:n}を {見上げる:v}
	yūgure sora o miageru
	В сумерках смотрим вверх на небо —
	At dusk we look up at the sky
```

- The first line must be `#!linguamap <version>`. Only version 1 is accepted; anything
  else is rejected rather than half-understood.
- Three headers are mandatory: `@columns`, `@source` and `@timeFormat`. All `@` headers
  must come before the first block.
- `@source` must appear in `@columns`; if it is not listed first the reader moves it to the
  front and warns, because the source line is always the block's first line.
- `[mm:ss.cc]` starts a block, and timestamps must strictly increase.
- The source line follows the timestamp; then exactly one tab-indented line per language in
  `@columns`, **excluding the source language**. A missing or extra column line is an
  error, not a silent shift.
- `@timeFormat none` marks the bundle as untimed; it is then accepted but shown by the
  plain renderer, which already handles unsynced lyrics and manual scrolling.

### Part of speech

Markers are `{text:code}` and live **in the source line only**. The whole unit goes inside
the braces, including any characters the marker's own class would otherwise be guessed for:

```
{灯り:n}が {揺れる:v}
```

A marker must not be used to annotate a *suffix* of a word: in `夕暮れ{起:q}` the `起` would
become part of the preceding unmarked unit, not a unit of its own.

An empty code, or plain text outside any marker, takes the class from `@posDefault`
(`part` when the header is absent). The codes are the published ones: `n`, `npr`, `v`,
`adj`, `adjna`, `adv`, `pron`, `conj`, `interj`, `part`, `q`.

A `{` that does not form a marker is literal text, so a lyric containing a brace needs no
escaping. `{}` is a separator placeholder that exists only to separate two markers.

Translation lines carry no markers, so their units all take `@posDefault`. That keeps the
format to one annotated line per block, at the cost of translations being uniformly
coloured; per-column annotation is a format-level change and not something a reader can
invent.

### Errors are line-precise

A malformed bundle is never accepted silently and never half-drawn. The reader reports the
line number and the reason, and the component falls back to the plain LRC parser so the
user still sees something. Real messages from the reader include:

```
line 1: the '#!linguamap' header has no format version
line 17: timestamp 9.00s does not come after the previous block's 10.00s
line 9: more column lines than the 3 required by @columns
line 23: the source line becomes empty once its unit markers are removed
block starting at line 31 has 1 column line(s) but 3 are required
```

`make check` runs the reader against the examples published with the format, a set of
tolerance cases (BOM, CRLF, literal braces, empty cells, hour-long timestamps, an `@offset`
and out-of-order headers) and nine rejection cases. It needs neither Windows nor
foobar2000, so CI runs it on every change.

## How it is drawn

A block is the source line followed by one indented line per translated column, in
`@columns` order. A cell that is empty adds no line at all, so a language that does not
cover a line does not leave a gap.

Colours come from the bundle's `@pos`/`@posDark` headers when the user has not switched
them off, and otherwise from the palette published alongside the format. `src/colorization.h`
is a verbatim copy of that palette and `make check-palette` proves it. The light or dark
half is chosen by asking foobar2000 whether the UI is in dark mode, so the panel stays
legible in either theme.

The current block is **not** recoloured. Wrapping it in the highlight colour would erase
the grammar colours at exactly the moment the eye is on them, so instead it gets a tinted
background and a rule down its left edge. Past blocks keep their hues and blend partway
toward the past-text colour, so scrolling back still reads as a word map.

Hovering a word dims every other word in the block, which shows which units belong
together when a translation reorders them. Clicking a block seeks to its timestamp.

Both the regular panel (GDI) and the external window (Direct2D) draw through the same
`layout_block` function in `src/ui_lyrics_map.cpp`. That function owns order, indents,
wrapping, gaps and colours; each window only turns the resulting runs into its own draw
calls. The alternative — two renderers each computing heights — produces a panel and a
window that scroll differently, which is precisely the bug the split prevents.

## Preferences

The **Display → Word maps** page controls:

- **Enable word maps** — master switch. Off means every track uses the plain renderer.
- **Show transliteration column** — whether the column named by `@translit` is drawn. It is
  drawn in a slightly dimmed colour so it reads as a phonetic mirror.
- **Use the bundle's colours** — off ignores `@pos`/`@posDark` and uses the built-in table.
- **Highlight the hovered meaning** — the dimming described above.
- **Past-block fade** — how far a past block blends toward the past-text colour, 0–100%.
- **Continuation indent** — pixels of indent for translation lines, or 0 to derive one em
  from the font size.
- **Never draw these languages** — comma-separated language keys to leave out.

## Limits

- Only format version 1 is understood.
- Bundles are read-only here; saving lyrics never rewrites a sidecar.
- Only the source line carries part-of-speech markers (see above).
- Word maps need timestamps to scroll; an untimed bundle falls back to the plain renderer.
