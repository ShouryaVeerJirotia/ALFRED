# ALFRED project log

This log records the dated implementation and verification state of the ALFRED prototype. It separates features present in source from behavior verified on a device. Never put API keys, passwords, access tokens, phone numbers, or other credentials in this log.

## 2026-11-08 — ALFRED 1.4.0 / firmware 2.5 release record

### Product and code changes

- Android companion: updated version to 1.4.0 (version code 5); added a foreground hands-free listening service, persistent notification with a Stop action, “Alfred” wake handling, and wake-to-follow-up command capture. In the foreground, the service resumes after Android TTS finishes “Yes?”; in the background, it queues the captured command until ALFRED is reopened.
- Android companion: retained native speech input/output, language selection, dashboard URL setting, app launch and music-provider intents, contact lookup, permission-gated calls, and one-shot location access. Android speech recognition, app music behavior, contacts and calling depend on the device and installed apps.
- ESP32-CAM firmware: advanced to v2.5 with corresponding dashboard voice handoff updates. The existing dashboard remains intended for phones, tablets and computers, with saved-Wi-Fi reconnect and `ALFRED-Setup` recovery access point behavior.
- Backend: updated the `alfred-assistant` Supabase Edge Function with live web-result excerpt fallback and Gemini summarization/source links when Google Search grounding fails. Gemini key rotation remains server-side; provider keys are not embedded in firmware or the APK.
- Desktop: added an optional localhost dashboard proxy and Codex code-edit bridge. It scopes work to a selected Git project, asks before starting a code task, and requires separate review/approval before applying a patch. It does not provide general desktop control or phone-to-PC pairing.
- Documentation: added setup, hardware scope, verification checklist, this dated project log, and a phased Jarvis roadmap.
- Safety/privacy: documented that the dashboard is unauthenticated on the local network, cloud memory is tied to the signed-in Supabase user, and ALFRED is supplementary information rather than a navigation aid. The hobby flame sensor is not a certified fire alarm.

### Current verification record

- Android 1.4.0 debug APK built successfully; package metadata and v1/v2 signatures were checked. SHA-256: `5e1dfd379df2585d9ec4242304d63cba631154b9a4aa9fe46242ea3911f1571f`.
- Firmware 2.5 compiled for AI Thinker ESP32-CAM with Arduino CLI and ESP32 core 3.3.10 (1,166,365 bytes program; 62,040 bytes RAM). It was flashed via CP2102 and esptool verified the written application hash. The serial monitor opened after reset, so a normal startup log was not captured.
- Supabase dashboard confirmed successful deployment of `alfred-assistant`. No signed-in end-to-end chat, memory or web-search request was verified during this update.
- No Android phone/emulator was connected to install the APK or verify background microphone, app launching, music playback, contacts, calls, location or TTS.
- The previous firmware v2.4 serial check showed repeated camera `FB-OVF` warnings, and the known dashboard status endpoint could not be reached from this computer. Firmware 2.5 camera, Wi-Fi, saved-network recovery and sensors remain to be tested on the assembled device.
- `git diff --check` passed before the release commit. A credential-pattern scan found no Google/Gemini API key strings in the publishable project files. The unredacted original firmware backup is ignored by Git; build outputs and local configuration are ignored.

### Known limitations and next verification steps

- Install APK 1.4.0 on a physical phone and test the wake phrase plus follow-up command with ALFRED visible and backgrounded, including screen lock, permissions, speech-service failure and notification Stop.
- On the assembled ESP32-CAM, confirm normal boot, camera stream/snapshots, sensor data and speech, saved Wi-Fi reconnect, router loss, and recovery AP setup.
- Sign into ALFRED cloud and verify chat, memory save/review/delete across devices, live search sources, key-pool failover and spoken responses. Check quota and plan eligibility.
- Verify music intents with the actual Spotify/YouTube Music apps and accounts; ALFRED may open provider search where direct playback is unsupported.
- Exercise desktop bridge against a test repository, including patch preview, apply, discard, stale patch and Codex failure.
- Reminders, SOS/trusted-contact sharing, QR/barcode reading, phone-to-PC pairing and arbitrary computer control remain future work.

## 2026-11-07 — preserved project baseline

- Initial ALFRED project baseline was preserved in Git as commit `21b8cb8`, including the v2.2 firmware baseline and Android wrapper.
- The untouched local firmware backup is excluded from version control because it contains a shared recovery-network password; a redacted backup copy remains tracked for reference.

## Release references

- Current firmware source: `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` (firmware version 2.5).
- Android source and build instructions: `android/`.
- Supabase Edge Function and schema migration: `backend/supabase/`.
- Desktop Codex bridge: `desktop/`.
- Current concise status and open checks: `STATUS.md` and `TEST_CHECKLIST.md` in this directory.
