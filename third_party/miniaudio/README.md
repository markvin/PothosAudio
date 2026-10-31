# miniaudio (a copy)

This directory has a copy of the single-file audio library miniaudio.
PothosAudio uses only its device input and output.

- Version: 0.11.25
- Source: https://github.com/mackron/miniaudio, tag `0.11.25`
  (the files `miniaudio.h` and `LICENSE`)
- SHA-256 of `miniaudio.h`:
  `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`
- License: public domain (Unlicense) or MIT No Attribution, refer to
  `LICENSE`

The build does not download this file. To update it:

1. Download `miniaudio.h` and `LICENSE` of the new release tag.
2. Replace the two files in this directory.
3. Write the new version and SHA-256 in this file.
4. Build PothosAudio and run its self-tests.
