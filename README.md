# ALFRED

ALFRED is an assistive-intelligence prototype built around an ESP32-CAM and a lightweight browser dashboard. The dashboard is intended for phones, tablets, and computers. The ALFRED-Showcase Supabase backend provides signed-in chat history and private long-term memory across devices.

The current source, firmware, Android companion, backend, setup notes, and product roadmap are in the [November 8 source package](https://github.com/ShouryaVeerJirotia/ALFRED/blob/main/ALFRED_Source_2026-11-08_release1.zip). The standalone firmware is also available as [ALFRED_Firmware_2026-11-08.ino](https://github.com/ShouryaVeerJirotia/ALFRED/blob/main/ALFRED_Firmware_2026-11-08.ino).

## Project map

- firmware/AlfredsFirmware_v2.2/ — working ESP32-CAM firmware.
- android/ — optional Android WebView companion source.
- backend/supabase/ — Edge Function and SQL migration for authenticated cloud AI and memory.
- desktop/ — localhost dashboard proxy and approval-gated Codex code-edit companion.
- docs/ — setup, hardware scope, status, verification checklist, and roadmap.

## Verification status

The November 8 firmware source compiles for AI Thinker ESP32-CAM with ESP32 Arduino core 3.3.10. It has not been flashed; the board is disconnected for this work, so normal-boot camera, Wi-Fi, voice, and sensor checks are pending. The preceding revision was flash-verified on November 7.

## Quick behavior

1. The ESP32 tries its saved Wi-Fi network at startup. If it cannot connect, it starts ALFRED-Setup; join from a phone or computer and open http://192.168.4.1/ to configure Wi-Fi.
2. After joining Wi-Fi, open the address shown by the device in any modern browser. The Android wrapper is optional.
3. For local Gemini mode, add your own key in Settings. It stays in page memory by default; choosing to remember it stores it in that browser profile's local storage without encryption.
4. Sign in to an ALFRED cloud account in Settings for cross-device chat and memory. The shared Gemini key pool is configured server-side; personal Gemini keys can also be entered. No provider key is embedded in the firmware.
5. Choose Hindi, English (India), or automatic Hindi/English in Settings. While the microphone is actively listening, a wake phrase is optional. Browser speech support may stop in the background or when the phone is locked.
6. On a computer, the optional desktop companion can let Codex prepare code changes in an isolated project copy. It requires the user to confirm each code task and separately approve its patch. It does not control other desktop apps or deploy code.

## Safety and privacy

ALFRED offers supplementary information for people who are blind or have low vision. It is not a navigation aid and does not replace a white cane, guide dog, or other established mobility aid. The flame sensor is not a fire alarm. The device dashboard is unauthenticated on the local network, so use a trusted Wi-Fi network. Cloud memory is private to the signed-in Supabase user. Camera images are sent only with vision requests.

The desktop companion binds to 127.0.0.1 and proxies the local ALFRED dashboard, providing a localhost origin for browser microphone access. A computer must run the companion itself. Google Search grounding can be used for current-information requests; availability and charges depend on Gemini model and account tier.
