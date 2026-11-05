# Cutscene dialogue missing (issues #3, #4): investigation 2026-11-04

## Repro (headless, ~4 min)

Open-world save, pause menu > GAME > Replay Mission > **Franklin and Lamar** (its intro is Michael at
the therapist, ~100 s of dialogue). Flags: `--gtav_trace_cutscene_audio=true` (pc_controls.cpp, logs
the cutscene audio entity 0x83A320F8 and its stream slots every 20 ms), `--xma_trace_buffers=true`
(SDK: every XMA input buffer swap / re-validation, and `touch /tmp/xma_state_request` dumps every
active context's registers), `--audio_dump_raw=<file>` (device mix). Pad navigation:
`tools` pad.sh sequence `Start; RB x4 (1 s apart); A; A; Down; A; (3 s) A`.

Symptom, every run: the guest's own 5.1 mix has the centre channel at -120 dB for the whole scene
(log line `audio: ... peak dB ... FC -120`), so it is not a downmix problem.

## What the trace shows

Audio entity states (sub_8243FB58): 2 = attach (stream created), 3 = prepared, waiting until the
cutscene clock reaches the audio start (+12 of the entry), 5 = playing (sub_823F3200).
Stream object: +124 position (ms), +136 end (ms), +128 start offset.

- 2 -> 3 with position 52 633 (the scene's offset in a 308 033 ms track), 3 -> 5 when the cutscene
  clock gets there (frame based: 0.9 s uncapped, 5.3 s at 30 fps; normal).
- Plays in real time to **59 668 ms (exactly 7.035 s) in every run**, then the position stops,
  ~10 s later the stream is dropped (position -1, state 0) and the rest of the scene is silent.

XMA contexts of the stream: two sets of three mono contexts (current block: 32/32/27 packets;
next block pre-loaded: 79/81/86 packets with start offsets 5455/11465/5483 bits). The 79- and
81-packet contexts decode the next block normally. **The 86-packet context (start bit 5483) never
yields usable output**, so when playback reaches the second block the stream stalls on that channel.

How it fails depends on the decoder mode:
- default (one decode pass per kick): the context sits at read offset 5483, output valid 0,
  write/read 0/0, never decoded again;
- `--xma_persistent_enable=true` (contexts stay enabled as on hardware): it runs through the whole
  86-packet buffer at once, produces 4 output blocks, marks the buffer done; the game never reads it.

## Fixed on the way (kept, default on; no change to the symptom)

- `StoreContextMerged` wrote the pass's snapshot of the output **read** offset back over the
  game's (the game advances it while a pass runs): only written now when the decoder changed it.
- "Output buffer full" (and the consume-only path) cleared output-valid for a buffer the game had
  not sized yet (0 blocks reads as no space + empty): now only after this pass filled it.
- cutscene_audio_fix.cpp also releases a deferred stream that stopped moving before its end.
- Open-world audio regression check after these: levels and decoder burst counts as before.

## Next

Decode that one buffer offline: dump the 86 packets (context input buffer 0 pointer, 86 x 2048
bytes) when it is submitted, and run it through the decoder from bit 5483 with tracing: frame
lengths, packet skip counts (multi-stream XMA2 interleaving: `GetPacketSkipCount`, next packet =
index + skip + 1), and where it gives up. Compare the 79/81-packet buffers, which decode.

## Later the same day

- Buffers dumped (`/tmp/xma_buf_*`, 64-byte register snapshot + packets): all stream blocks have
  normal XMA2 packet headers (skip 0, metadata 1); the 86-packet one just has 11-13 frames/packet.
- Per-frame trace: every context's first pass decodes 4 frames; with the one-frame carry delay that
  is 3 x 4 = 12 output blocks = the whole 12-block ring, so write == read ("empty"). For the playing
  block the game drains it at once; the pre-loaded block sat "full = empty".
- `--xma_output_keep_free_block` (never fill the ring; default off) makes the 86-packet context hold
  8 readable blocks, but the game still never reads it at the switch while it reads the 79/81 ones
  (their contexts differ in output_buffer_padding: 3/2 vs 0 at submit time, and the 86 one is
  submitted with read offset 32, the others with a seek offset). Still stops at 59 668 ms.
- Next: what makes the game start reading a pre-loaded channel (look for the guest code reading
  output_buffer_read_offset / write_offset of these contexts around the block switch).
