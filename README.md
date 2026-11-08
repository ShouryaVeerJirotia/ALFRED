# ALFRED

ALFRED is a compact ESP32-CAM assistive-intelligence prototype with a browser dashboard. The device serves one responsive UI to phones, tablets and computers; the ALFRED-Showcase Supabase backend provides authenticated chat history and long-term memory across devices.

The complete source, backend, Android companion, setup notes and assistant roadmap are in this project folder and the [ALFRED GitHub repository](https://github.com/ShouryaVeerJirotia/ALFRED). The firmware sketch is `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` (current firmware version 2.5).

## Project map

- `backups/AlfredsFirmware_v2.2_original_2026-11-07.ino` — untouched local-only source backup.
- A redacted source copy is included for review; the original is excluded from GitHub because it contains a shared recovery-network password.
- `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` — working copy for firmware updates.
- `android/` — existing Android WebView companion source.
- `backend/supabase/` — Edge Function and SQL migration for authenticated cloud AI and memory.
- `desktop/` — localhost dashboard proxy and approval-gated Codex code-edit companion.
- `docs/` — setup, hardware scope, status, dated project log, verification checklist and Jarvis roadmap.

## Verification status

Firmware v2.5 was flashed to the AI Thinker ESP32-CAM and esptool verified the written application hash. Its normal startup log was not captured, and the v2.4 run had repeated `FB-OVF` warnings with an unreachable `/status` endpoint, so Wi-Fi and camera output still need checking. Android 1.4.0 and firmware v2.5 builds succeeded. The Supabase function now falls back to live web excerpts and an AI summary when Google Search grounding fails; deployment succeeded, but a live signed-in query still needs device verification. Phone installation, microphone behavior, sensor readings and search still need a real-device check. The debug APK is for testing; Play Store publishing requires a release-signed build. See [status](docs/STATUS.md) and the [verification checklist](docs/TEST_CHECKLIST.md).

## Quick behavior

1. The ESP32 tries its saved Wi-Fi network on boot. If it cannot connect, it starts `ALFRED-Setup`; join it from any phone or computer and open `http://192.168.4.1/` to configure Wi-Fi.
2. Once joined to Wi-Fi, open the address shown by the device in any modern browser. The Android wrapper is optional.
3. To use a personal Gemini key, enter it in Settings. By default it stays in page memory; enabling “Keep keys in this browser” saves it in that browser profile’s local storage without encryption.
4. In Settings, create or sign in to an ALFRED cloud account. The Supabase project details are prefilled, and a rotating shared Gemini key pool is configured server-side. You can also enter your own Gemini keys; the firmware contains no Gemini provider key.
5. In Settings → Behaviour, choose Hindi, English (India), or automatic Hindi/English. In the Android app, hands-free listening starts after microphone and notification permissions are granted. The phone displays an ongoing microphone notification with a Stop action and listens for “Alfred.” After the wake phrase, the updated Android service listens for the next utterance; if the activity is closed, it queues that command for processing when you reopen ALFRED. Force-stop or revoked permissions stop listening. This is not the same as Alexa's system-level wake-word access.
6. In Android Phone actions, choose Any app, Spotify or YouTube Music. ALFRED asks the selected music app to play the matching track; it opens the provider's search if the app cannot play directly. Contact lookup asks for Contacts permission only when a name-based call is requested. Current location asks for location permission when requested; sharing or calling still requires a confirmation.
7. To use Codex for project edits on a computer, install and sign in to Codex CLI, then run `python3 desktop/alfred_desktop.py --device-url http://alfred.local --workspace /absolute/path/to/your/project` from this repository. If `alfred.local` does not resolve, use ALFRED's current private Wi-Fi IP. Open `http://127.0.0.1:8765` in that computer's browser. A coding request is confirmed before Codex starts; Codex works in an isolated copy, and ALFRED displays a patch for your separate approval before applying it. This first bridge only edits the selected Git project; it does not control other desktop apps or deploy code.

## Safety and privacy

ALFRED is supplementary information for people who are blind or have low vision. It is not a navigation aid or substitute for a white cane, guide dog, or other established mobility aid. The flame sensor is not a fire alarm. The ESP32 dashboard is currently unauthenticated on the local network; use a trusted Wi-Fi network. Cloud memory is private to the signed-in Supabase user. Camera images are sent only with vision requests.

The desktop companion binds only to `127.0.0.1` and proxies ALFRED's local dashboard, which also gives desktop browsers a `localhost` page for microphone access. A computer must run the companion itself; the ESP32 never accepts arbitrary computer commands. Gemini Search grounding is enabled by default for current-information prompts in new browser profiles and can be switched off in Settings. In signed-in cloud mode, if Gemini Search is rejected or quota-limited, Supabase fetches live web excerpts and asks Gemini to summarize them with source links; if summarizing also fails, ALFRED returns the links. Search grounding and the fallback still depend on provider and network availability. Neural TTS may use the free tier, but Google's pricing page says free-tier content can be used to improve products. Choose device speech in Settings if that tradeoff is not acceptable.
