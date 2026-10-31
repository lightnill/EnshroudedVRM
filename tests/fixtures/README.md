# External speech test input

The speech audio is deliberately not redistributed in this repository or the player ZIP. It is needed only to reproduce `test_voice_automatic.py`; players do not need it.

Download [tests/test16000Hz.wav from the pinned HeadAudio commit](https://raw.githubusercontent.com/met4citizen/HeadAudio/c6d8af35034068b1c98dcc0fa3ec9602d711e784/tests/test16000Hz.wav) and save it here as `voice-public-speech.wav` before running the full developer suite.

Expected size: **672,122 bytes**. Expected SHA-256:

`6318883d5a5d5dc4b95457e43e2bf88d934b187f850a46b52ef02b2f459fb775`

The test verifies that digest before processing the audio. Retain [the supplied MIT license](voice-public-speech-LICENSE.txt), Copyright (c) 2025 Mika Suominen. [Provenance](voice-public-speech-PROVENANCE.json) records the upstream commit, original path and Git blob identity. No microphone capture or user recording is required. The reported full-suite run used this separately supplied fixture.
