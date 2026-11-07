# ALFRED

ALFRED is a compact ESP32-CAM assistive-intelligence prototype with a browser dashboard. The device serves one responsive UI to phones, tablets and computers; the ALFRED-Showcase Supabase backend provides authenticated chat history and long-term memory across devices.

The complete source, backend, Android companion, and setup notes are in the [cloud-ready source package](https://github.com/ShouryaVeerJirotia/ALFRED/blob/main/ALFRED_Source_2026-11-07_CloudReady.zip). The standalone firmware file is also available at the repository root as `AlfredsFirmware_v2.2.ino`.

## Project map

- `backups/AlfredsFirmware_v2.2_original_2026-11-07.ino` — untouched local-only source backup.
- A redacted source copy is included for review; the original is excluded from GitHub because it contains a shared recovery-network password.
- `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` — working copy for firmware updates.
- `android/` — existing Android WebView companion source.
- `backend/supabase/` — Edge Function and SQL migration for authenticated cloud AI and memory.
- `docs/` — setup, hardware scope, security, status and verification checklist.

## Verification status

The previous firmware build was flashed and verified through the CP2102 on 2026-11-07. The latest revision adds the ALFRED-Showcase connection defaults and compiles for AI Thinker ESP32-CAM with ESP32 Arduino core 3.3.10, but the latest upload has not completed: the ESP32 returned no serial data during the bootloader handshake. Camera, Wi-Fi, speech, and sensor behavior still requires a normal-boot hardware check.

## Quick behavior

1. The ESP32 tries its saved Wi-Fi network on boot. If it cannot connect, it starts `ALFRED-Setup`; join it from any phone or computer and open `http://192.168.4.1/` to configure Wi-Fi.
2. Once joined to Wi-Fi, open the address shown by the device in any modern browser. The Android wrapper is optional.
3. For local Gemini mode, enter your own Gemini API key in Settings. It is kept in that browser profile if you enable “Remember on this browser.”
4. In Settings, create or sign in to an ALFRED cloud account. The Supabase project details are prefilled. Enter your Gemini key under Settings to use AI now; shared server keys can be added by the project owner in Supabase Edge Function Secrets. The firmware contains no Gemini provider key.

## Safety and privacy

ALFRED is supplementary information for people who are blind or have low vision. It is not a navigation aid or substitute for a white cane, guide dog, or other established mobility aid. The flame sensor is not a fire alarm. The ESP32 dashboard is currently unauthenticated on the local network; use a trusted Wi-Fi network. Cloud memory is private to the signed-in Supabase user. Camera images are sent only with vision requests.
