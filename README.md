# TextReader

A Windows viewer for text files tens of gigabytes in size.

A file is never loaded entirely into memory. With this solution, opening a 40 GB file requires ~9 MB of memory in addition to the empty application (and almost all of that is a sparse line index). Scrolling, searching, and navigating through lines are just as fast for a 1 KB file as they are for a 40 GB file.

## Prerequisites

**To run:** Windows 10 or later, a GPU with Direct3D 11 support. The CRT is linked statically, so no installation is required: no Visual C++ Redistributable is needed + the exe imports only system libraries.

**To build:** Visual Studio 2022 (Build Tools are enough), CMake 3.28 or newer. The first configure needs git and an internet connection (CMake downloads FreeType through FetchContent).

## What it is built on

| Technology | What for                                                                                                                                    |
|---|---------------------------------------------------------------------------------------------------------------------------------------------|
| **C++20** | `std::jthread` and `std::stop_token` for all background tasks and cancellation; `std::span` for views into existing buffers without copying |
| **Win32 API** | Window and message loop, all file I/O, file mapping to memory, temporary files, free disk space                                             |
| **Direct3D 11** | ImGui rendering backend (swap chain, render target)                                                                                         |
| **Dear ImGui** | UI and drawing primitives, immediate mode                                                                                                   |
| **FreeType** | Font rasterization instead of the built-in `stb_truetype` (results in sharper text for monospaced font)                                     |
| **WinHTTP** | HTTP and HTTPS download using a system library (adds no dependencies to the exe)                                                            |
| **COM, `IFileDialog`** | Open and Save dialogs (that is all the system UI in the app)                                                         |
| **CMake + MSVC** | Build. ImGui is bundled with the project, FreeType via `FetchContent`                                                                       |
| **JetBrains Mono** | Monospaced font shipped next to the exe                                                                                                     |

## Getting started

**Prebuilt.** Download `TextReader-win-x64.zip` from [Releases](https://github.com/menshiva/textreader/releases), unpack it, and run `TextReader.exe`. Keep the `fonts` folder next to the exe.

**From source code:**

```bash
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64
cmake --build build-release --config Release --parallel
```

The exe will be located in `build-release/Release/`, and the fonts will be copied there automatically. It compiles with `/W4` without a single warning.

## Features and how the assignment is covered

| Requirement                                                      | How it is covered                                                                       |
|------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| Very large files (tens of GB)                                    | Sparse index, sliding mapping window, streaming reads. Tested on 40 GB                  |
| Source: txt file                                                 | `Ctrl+O`                                                                                |
| Source: web address                                              | `Ctrl+U`, WinHTTP, HTML displayed as text                                      |
| Source: random long text                                         | `Ctrl+G`, a size in KB/MB/GB or a number of lines                                       |
| Save to file                                                     | `Ctrl+S`                                                                                |
| Search: `Ctrl+F`, `F3` / `Shift+F3`, scroll and focus            | The match is highlighted and selected; if it is off screen, it is brought to the center |
| Millions of lines, memory bounded by data rather than by the GUI | Virtualization: only the visible characters are drawn each frame                        |
| Smooth scrolling                                                 | Positioning accurate to a fraction of a line, with animated transitions                  |
| No out-of-the-box editor/viewer components                       | Text, line numbers, scrollbars, selection and highlighting are written from scratch     |

Additional features: UTF-8 with and without BOM, `LF` and `CRLF`, mouse selection and `Ctrl+A`, copying with `Ctrl+C`, cancellation and progress for every long-running operation.

## Controls

| Keys | Action                                                           |
|---|------------------------------------------------------------------|
| `Ctrl+O` / `Ctrl+U` / `Ctrl+G` / `Ctrl+S` | Open a file / from a URL / generate / save as                    |
| `Ctrl+F` | Search panel                                                     |
| `Enter`, `F3` / `Shift+F3` | Next / previous match                                            |
| `Esc` | Close the search and clear the highlight                         |
| `Home` / `End` | To the start of the line / to the end of the longest visible one |
| `Ctrl+Home` / `Ctrl+End` | To the start / end of the file                                   |
| `PgUp` / `PgDn` | Scroll up / down one screen                                      |
| `Ctrl+A` / `Ctrl+C` | Select all / copy                                                |
| Mouse wheel | Vertical scroll, horizontal scroll with `Shift`                  |

Scrollbars can be dragged with the mouse, and so can the selection, with auto-scroll at the edge. Copying is limited to 50 000 lines or 16 MB. Searching does not require indexing to be finished.

## Why C++, Dear ImGui and Direct3D 11

I have experience with C++ and ImGui, but almost no experience with C#/.NET. With an unfamiliar tech stack, I’d end up spending a significant amount of time on the tools instead of the task itself, so I went with what I know.

## How it works

> **System specs:** Intel Core i7-10700K (8 cores / 16 threads, 3.8 GHz), 32 GB DDR4-3200, test files and `%TEMP%` on an NVMe SSD (WD Black, 1 TB), Windows 11, Release build.
> Test files: 1 GB - fits in the system cache; 40 GB (83 636 159 lines) - does not.

**Line index.** The naive approach would be an array of offsets for all rows. For 40 GB that's 83 636 159 entries of 8 bytes each, **669 MB** for the index alone. That was ruled out immediately. Instead, I store the offset of every 128-kilobyte boundary: 327 680 anchors, 16 bytes each - **5.2 MB**. A line is found using a binary search over the anchors plus a `memchr` scan, no more than 128 KB per request. The spacing came out of a sweep: jump latency is linear (32 KB - 0.054 ms, **128 KB - 0.078 ms**, 1 MB - 0.298 ms), while memory is inversely proportional (32 KB - 21.0 MB, **128 KB - 5.2 MB**, 1 MB - 0.7 MB).

**Two reading mechanisms.** Keeping both a memory-mapped view and a plain `ReadFile` seems redundant, so I twice tried to use a single one and both times I had to roll back:

| Scenario                                   | Mapping | `ReadFile`       | Mapping page faults |
|--------------------------------------------|---|------------------|---------------------|
| Jump to a line, 40 GB                      | **0.286 ms** | 0.467 - 0.539 ms | 43 965              |
| Full read from an offset to the end, 40 GB | 62.9 s | **21.6 s**       | 10 511 390          |

The rule I've derived from this: **a mapping wins when you load significantly more than you actually use, `ReadFile` wins when you use everything you've loaded.** Rendering asks for up to 128 KB, but `memchr` stops at the first line break and touches less than one percent of it (mapping case). Search goes through everything up to the end of the file (`ReadFile` case). Neither "random versus sequential access" nor the window size mattered. The conclusion: `LineIndexer` holds both handles.

**Counting UTF-8 code points.** The indexer counts the characters in each line to determine column positions. The byte-by-byte approach turned out to be the bottleneck of the entire indexing process: 26.15 s on 40 GB, compared to **21.91 s** for the selected word-at-a-time approach. I took [the approach from the Rust standard library](https://doc.rust-lang.org/src/core/str/count.rs.html).

**Scroll position.** I store the index of the first visible line as an integer, and only the remainder inside that line in pixels. Smoothness also comes from the same place: transitions using `Home`, `End`, `PgUp`, `PgDn` and to a found match are animated over 100 ms. Only the visible characters of the visible lines (usually around 50) are drawn each frame, without a single allocation (`get()` returns a view into the mapping window).

**Search.** It runs over raw bytes using the Boyer–Moore–Horspool algorithm (available out-of-the-box in the C++ standard library). For UTF-8, this works correctly without a single line of code related to Unicode: a continuation byte always has the form `10xxxxxx` and cannot be either the start of a character or a single-byte character, so a byte match will never turn out to be in the middle of another character. A side effect is that Cyrillic and Czech become searchable for free. That is precisely why the search is case-sensitive: for non ASCII, it would require more complex processing and would lose the benefits of byte-by-byte comparison. A match at the boundary between 2 buffers is detected by a "seam" built from the tail of the previous block and the head of the current one. Backward search proceeds in 4 MB windows, looking for the last match inside each one (otherwise it would have to walk from the start of the file every time). With windows, a match near the end of a 40 GB file is found in **2.7 ms** instead of 22 s.

**Download.** Built on WinHTTP, synchronous API on a background thread. The response body is read **directly into the file writer's buffer** (WinHTTP documentation explicitly [recommends](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpreaddata) reading right after `WinHttpReceiveResponse` to avoid unnecessary copying). HTML is saved and displayed as plain text.

**Background work and cancellation.** Every long-running operation runs on a background thread through `Job<Payload>` and `AsyncTask` on top of `std::jthread` and `std::stop_token` (UI never blocks). Progress is published into a `std::atomic<float>`, which the UI reads once per frame. The `stop_token` is checked withing the working loop, so Cancel takes effect on the next process iteration.

**Writing.** Uses `FILE_FLAG_NO_BUFFERING`. The price: the buffer has to be aligned to a sector boundary + writes have to be a multiple of a sector, so the tail is padded with zeroes and the actual end of file is set separately. The size is reserved in advance. Saving an open file uses a simpler approach: it uses `CopyFileExW`, which reports progress itself and is canceled through the same callback.

### Tuned constants

- **Anchor at every 128 KB.** Memory / latency trade-off, discussed above.
- **Indexer scan buffer, 4 MB.** 1 and 4 MB are indistinguishable (699 ms on the 1 GB file), while 16 and 64 MB are slower (760 and 781 ms) and cost more memory. Of the two equal options, 4 MB was chosen (the same size as the other buffers).
- **Search buffer, 4 MB.** 1 MB - 23.7 s, **4 MB - 22.8 s**, 16 MB - 35.7 s, 64 MB - 43.5 s. Roughly 1.6x worse in both directions.
- **Backward search window, 4 MB.** In a typical case (the match is nearby and the very first window finds it) the performance gain is linear: 4.0 ms vs 36.2 ms for 64 MB. In the worst case (there is no match in the file at all and all 40 GB are walked backwards) 4 MB is actually the worst choice: 43.4 s vs 42.5 s. This is a rational trade-off: a 9x gain in typical cases versus a 2% loss in rare cases.
- **Mapping window, 4 MB.** Latency is the same regardless of size, and so is the number of page faults (24 467 in all cases - they follow the amount of data accessed, not the window size). 16 MB pays for an extra 3.5 MB of working set for no reason, 64 MB adds a 32.7 ms stall for remapping.
- **Write buffer, 4 MB.** 1 MB gives 2013 MB/s, 4 MB gives 2225, and beyond that the gain is less noticeable (16 MB gives 2247 at twice the working set).

## Memory

Process memory after indexing is fully complete:

| State                         | Memory      |
|-------------------------------|-------------|
| Running, file not open | 11.8 MB     |
| 838 KB file open              | 15.8 MB     |
| 40 GB file open               | **20.6 MB** |

A 50 000x increase in data size takes up ~5 MB. The constant 11 MB consists of Direct3D, the font atlas and ImGui itself (they do not depend on the file at all).

Also: the peak working set of the scanning alone, excluding the graphics part, is **13.8 MB** for a full 40 GB file.

## Limitations

- **Windows only.** Win32, Direct3D 11, WinHTTP and Windows-specific file I/O.
- **UTF-8 only.** With and without BOM, `LF` and `CRLF`. Other encodings are not detected.
- **Search is case-sensitive.** Explained above.
- **No match counter.** On 40 GB that would be a separate full file pass.
- **Lines longer than 64 KB are truncated when displayed.** This guards against files with no line breaks at all (e.g. a single-line JSON of several gigabytes).
- **One open file at a time**, no tabs. The file is opened with `FILE_SHARE_READ` though, so other applications can read it in parallel.

## Dependencies and licenses

TextReader itself is released under the [MIT License](LICENSE). The bundled and fetched components keep their own:

| Component | License |
|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [FreeType](https://freetype.org/) | FTL or GPLv2 |
| [JetBrains Mono](https://www.jetbrains.com/lp/mono/) | SIL Open Font License 1.1 |

The font license text (`OFL.txt`) ships next to the exe.
