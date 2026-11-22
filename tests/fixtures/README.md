# CineRTX Lightweight Test Fixtures

Physical mini Matroska fixtures for the realtime playback tests (Phase 11.2-R
R-4: the 1,306-line `test_streams.h` hex array is gone from the source tree;
the same container bytes now live here as real files, deployed beside
`rtx_tests.exe` by a CMake POST_BUILD copy).

- `h264_25fps_gop12.mkv`  1.0 s / 25 fps / 320x240 H.264 High 8-bit + 440 Hz AAC mono
- `hevc_25fps_gop12.mkv`  same geometry, HEVC Main10
- `av1_25fps_gop12.mkv`   same geometry, AV1 Main 10-bit

GOP 12 (IDR at frames 0/12/24); video was muxed WITHOUT audio first and then
`-c copy`-merged with the AAC track, so no AAC priming offset shifts the 40 ms
frame grid.  Tests read the bytes via `<fstream>` and drive
`makeMemoryDemuxer()`.  AuditSpdx.cmake only scans source extensions, so the
binary fixtures stay out of its scope by design.
