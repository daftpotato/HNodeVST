# HNodeVST

A VST3 version of [HNode](https://github.com/Happyrobot33/HNode) — the open-source
Art-Net DMX → video grid node renderer used to drive VRChat stage lighting through
VRSL.

It runs inside a DAW, so the grid renderer becomes a plugin you can automate,
side-chain to the transport, and drive from MIDI, instead of a standalone window.

## What it does

```
Art-Net DMX ─┐
MIDI ────────┼─► generators ─► serializer ─► grid texture ─► Spout2 ─► OBS ─► stream ─► VRChat
OSC ─────────┘                      │
                                    └─► Art-Net out / OSC out
```

- **Art-Net in** on UDP 6454 (a lighting console can still feed it)
- **MIDI in** — notes and CC mapped onto DMX channels
- **OSC in** — `/VRSL/<channel> <value>`
- **Generators** — strobe, fade, static, time-of-day, per host BPM
- **Serializer** — VRSL (the VRChat standard) plus Binary, Color Binary, Ternary,
  Spiral and BinaryStageFlight (MDMX), ported 1:1 from HNode's source
- **Spout2 out** — sender name defaults to `HNode VST Output`, ready for an OBS
  Spout2 Capture source
- **Art-Net out** and **OSC out** (defaults to `/VRSL` on 12000, matching VRSL's
  `GridReader`)

## Install

The plugin is a VST3 bundle:

```
HNodeVST.vst3/
└── Contents/
    ├── Resources/
    └── x86_64-win/
        ├── HNodeVST.vst3      <- the plugin
        └── SpoutLibrary.dll   <- Spout2 runtime, must stay beside it
```

Copy the whole `HNodeVST.vst3` folder into a VST3 folder your DAW scans:

- **System (all users):** `C:\Program Files\Common Files\VST3\` — needs admin
- **Per-user:** `C:\Users\<you>\Documents\VST3\` — then enable
  *Live → Preferences → Plug-Ins → VST3 Custom Folder* and browse to it

`build/install-system.bat` copies it to the system folder (run as administrator).

## Using it

1. Insert **HNode VST** as an instrument on a MIDI track.
2. Enable **Spout Output** and confirm the sender name in the editor.
3. In OBS, add a **Spout2 Capture** source and pick `HNode VST Output`.
4. Point the OBS stream at your world's video player (MediaMTX locally, or a CDN).
5. Drive the lights from Art-Net, from MIDI clips, or with the generators.

The editor shows a live preview of the grid, a channel strip for the first 64
channels, and a status line with FPS / channel count / output state.

## Verified behaviour

`build/smoketest.cpp` loads the built `.vst3` the way a host would and checks the
serialiser against hand-computed values. Current results:

```
[PASS] ch0 = 128 -> linear      got 55  want 55
[PASS] ch13 = 255 -> linear     got 255 want 255
[PASS] ch26 = 0 -> linear       got 0   want 0
[PASS] alpha opaque             got 255 want 255
[PASS] channels serialized == 1536
[PASS] frames rendered > 0
```

The 128 → 55 case is the meaningful one: it confirms the gamma path
(`sRGB → linear`, matching HNode's `Color.linear`) is correct, not just present.

## Building

The two third-party SDKs are **not** committed (they are large and carry their
own licences). Fetch them once, then build:

```
cd HNodeVST
build\fetch-sdk.bat     :: downloads + assembles sdk/   (one time)
build\build.bat         :: builds the .vst3 into build/out/
build\smoketest.bat     :: builds + runs the verification harness
```

Requirements: Visual Studio 2017 BuildTools (MSVC 14.16) or newer, Windows 10/11
x64. `fetch-sdk.bat` needs `curl` and PowerShell (both present on Windows 10/11).

If you move the SDKs somewhere else, edit the `SDK=` / `SPOUT=` paths at the top
of `build\build.bat`.

## Notes / limitations

- **OSC wire format is an assumption.** HNode's grid protocol is carried by
  `TekView.TekParse`, which is not public, so the outgoing address format
  (`/VRSL/<channel> <value>`) is a best guess. The video path — the one that
  actually drives the lights — is a faithful port and is verified above.
- Only the VRSL, Binary, ColorBinary, Ternary, Spiral and MDMX serialisers are
  ported. HNode's MAVLink drone, Twitch chat, SRT/ASS/LRC and snapshot
  generators are not.
- Audio passes straight through unchanged; the DMX work runs on the audio
  thread but is throttled to the configured frame rate.
- HNode is MIT-licensed; the VRSL grid node itself is a paid product. This is a
  re-implementation of the open-source renderer, not a copy of it.
