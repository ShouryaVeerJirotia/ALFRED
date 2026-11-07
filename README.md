# ALFRED

ALFRED is a compact ESP32-CAM assistive-intelligence prototype with a browser dashboard. The device serves one responsive UI to phones, tablets and computers; the ALFRED-Showcase Supabase backend provides authenticated chat history and long-term memory across devices.

The complete source, backend, Android companion, setup notes and assistant roadmap are in the [cloud-ready source package](https://github.com/ShouryaVeerJirotia/ALFRED/blob/main/ALFRED_Source_2026-11-07_CloudReady_v4.zip). The standalone firmware file is also available at the repository root as `AlfredsFirmware_v2.2.ino`.

## Project map

- `backups/AlfredsFirmware_v2.2_original_2026-11-07.ino` — untouched local-only source backup.
- A redacted source copy is included for review; the original is excluded from GitHub because it contains a shared recovery-network password.
- `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` — working copy for firmware updates.
- `android/` — existing Android WebView companion source.
- `backend/supabase/` — Edge Function and SQL migration for authenticated cloud AI and memory.
- `docs/` — setup, hardware scope, status, verification checklist and Jarvis roadmap.

## Verification status

The latest firmware, including bilingual voice selection and safer key-storage defaults, compiled for AI Thinker ESP32-CAM with ESP32 Arduino core 3.3.10 and was flash-verified through the CP2102 on 2026-11-07. Normal-boot camera, Wi-Fi, speech, and sensor behavior still needs hardware verification.

## Quick behavior

1. The ESP32 tries its saved Wi-Fi network on boot. If it cannot connect, it starts `ALFRED-Setup`; join it from any phone or computer and open `http://192.168.4.1/` to configure Wi-Fi.
2. Once joined to Wi-Fi, open the address shown by the device in any modern browser. The Android wrapper is optional.
3. To use a personal Gemini key, enter it in Settings. By default it stays in page memory; enabling “Keep keys in this browser” saves it in that browser profile’s local storage without encryption.
4. In Settings, create or sign in to an ALFRED cloud account. The Supabase project details are prefilled, and a rotating shared Gemini key pool is configured server-side. You can also enter your own Gemini keys; the firmware contains no Gemini provider key.
5. In Settings → Behaviour, choose Hindi, English (India), or automatic Hindi/English selection. Browser listening is started by the user and depends on browser support; it is not guaranteed to work with the page backgrounded or the phone locked.

## Safety and privacy

ALFRED is supplementary information for people who are blind or have low vision. It is not a navigation aid or substitute for a white cane, guide dog, or other established mobility aid. The flame sensor is not a fire alarm. The ESP32 dashboard is currently unauthenticated on the local network; use a trusted Wi-Fi network. Cloud memory is private to the signed-in Supabase user. Camera images are sent only with vision requests.
