# C++ / Fcitx5 Development Rules
- Absolutely do NOT hallucinate or guess Fcitx5 header names (e.g. fcitx/inputevent.h, fcitx/connection.h do NOT exist).
- Only include existing headers found under `/usr/include/Fcitx5/`.
- Always inspect the provided read-only headers (instance.h, event.h, addoninstance.h) for exact types and signal signatures.
- Key events must be monitored via signals defined in `instance.h` (e.g., KeyEventResult).
