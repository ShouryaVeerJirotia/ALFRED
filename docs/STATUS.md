# ALFRED status (2026-11-08)

## Latest update: wake-to-command and live search (Android 1.4.0 / firmware v2.5)

- The Android hands-free service keeps listening after a bare “Alfred” wake phrase. In the foreground it pauses while Android speaks “Yes?” and resumes on the TTS completion callback. In the background it captures a follow-up and queues it for processing when ALFRED is reopened. This depends on Android's speech recognizer and is not Alexa-style system hotword access.
- The Supabase `alfred-assistant` Edge Function is deployed. It tries Gemini Google Search grounding, then fetches live search-result excerpts for Gemini to summarize with source references; if summarization fails, it returns clickable results. A signed-in, end-to-end search request has not been tested.
- The cloud search fallback needs a signed-in ALFRED account and an available Gemini text-generation key. In local API-key-only mode, a failed Gemini search returns a Google Search results link.
- Android 1.4.0 and firmware v2.5 contain the wake handoff changes. Firmware v2.5 was flashed through CP2102 and esptool verified the written application hash. The serial monitor was opened after reset, so this flash's normal startup log was not captured.

## Verification

- Android debug APK v1.4.0 (version code 5) built successfully; package metadata and APK v1/v2 signatures were checked. SHA-256: `5e1dfd379df2585d9ec4242304d63cba631154b9a4aa9fe46242ea3911f1571f`. No Android handset/emulator was attached for installation or microphone behavior testing.
- Firmware v2.5 compiled successfully with Arduino CLI and ESP32 core 3.3.10 (1,166,365 bytes program, 62,040 bytes RAM), then was flashed and its application hash verified. Normal startup, camera, Wi-Fi and sensors still need hardware verification.
- Supabase dashboard confirmed the Edge Function deployment. Live search, Gemini summarization, memory and spoken playback have not been exercised end-to-end from a signed-in device.
- An earlier v2.4 hardware check showed repeated camera `cam_hal: FB-OVF` warnings; the known `/status` IP and `alfred.local` were unreachable from this computer. Recheck camera and Wi-Fi on v2.5.
- The final source and dated project log are being recorded in the private ALFRED GitHub repository alongside this local checkout. Build artifacts and credentials are excluded.

## Implemented in source

- ESP32-CAM dashboard, camera snapshots/vision prompts, sensor telemetry, Wi-Fi recovery and `ALFRED-Setup` provisioning.
- Gemini chat and Search grounding, Supabase sign-in, saved memories, cross-device text history, and server-side Gemini key rotation. Provider keys are not hardcoded into firmware or the APK.
- Android companion with native voice I/O, hands-free foreground service, app launch/music intents, contact lookup, permission-gated calls, and one-shot location access.
- Local desktop Codex bridge with explicit confirmation and patch review before source changes are applied.

## Remaining checks and limitations

- Install APK 1.4.0 and test “Alfred” followed by a command with the app visible and in the background. Confirm microphone/notification permissions and speech-service settings.
- Test live search while signed in to ALFRED cloud, including source links and the live-excerpt fallback. Check API quota and eligibility with the account/key.
- Recheck camera, Wi-Fi, and connected sensors after flashing. ALFRED is supplementary information, not a navigation aid or substitute for a white cane, guide dog, or other established mobility support. The flame sensor is not a certified fire alarm.
- Persistent reminders, SOS/trusted-contact sharing, QR/barcode reading, automatic phone-to-PC pairing, and arbitrary desktop app control remain future work.
