# Setup and deployment

## ESP32-CAM

1. Open `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` with Arduino IDE. Select **AI Thinker ESP32-CAM**, enable PSRAM, and use the ESP32 Arduino core noted in the sketch header/audit.
2. Build and upload the current sketch from Arduino IDE. For uploads, connect GPIO0 to GND, tap RST/EN, and keep GPIO0 grounded during flashing. After the upload finishes, remove the GPIO0-to-GND jumper and press RST/EN once so the sketch starts normally. If GPIO0 remains grounded, the board stays in programming mode instead of running ALFRED.
3. On first boot, join Wi-Fi `ALFRED-Setup` (the setup password is unique to this board and is printed to Serial Monitor only when recovery AP starts) and visit `http://192.168.4.1/`.
4. In Network, scan/select the 2.4 GHz Wi-Fi, enter its password, and save. After ALFRED connects, open `http://alfred.local/` where mDNS is supported, or use the DHCP IP shown on screen. The setup page is plain HTTP on the local network, so only provision on a network you trust.
5. Open the dashboard URL in a current phone/tablet/desktop browser. Keep ALFRED and the phone/computer on the same network. Browser speech recognition depends on browser and OS support; typed requests and spoken replies are the fallback paths.
6. Under Settings → Behaviour, choose English (India), Hindi, or automatic Hindi/English voice selection. Turn on the consistent ALFRED neural voice for the same Gemini-generated voice across devices; if TTS is unavailable, the device's installed voice is used.
7. In the Android app, open Settings and start **Hands-free listening**. Android shows an ongoing microphone notification with a Stop action. Say “Alfred”; after each response, ALFRED accepts follow-up speech for 20 seconds. This uses the phone's speech-recognition service and needs microphone and notification permissions. Browser mic mode remains user-started and may stop when the page is backgrounded or the phone is locked.
8. In the app's Phone actions, choose Any app, Spotify or YouTube Music before asking ALFRED to play a track. Android sends a play-from-search request to that provider. If the app does not implement direct playback for search, ALFRED opens its search page. Select a contact using **Contacts**; AI-requested calls require ALFRED's confirmation, and Android asks for call permission before placing a direct call.
9. On a computer, install and sign in to Codex CLI, then start `python3 desktop/alfred_desktop.py --device-url http://alfred.local --workspace /absolute/path/to/a/git/project`. Replace the device URL with its current private IP if mDNS is unavailable. Open `http://127.0.0.1:8765` in the same computer browser. Code tasks are confirmed, run in an isolated source copy, and displayed as a patch for a separate apply/discard choice. This first bridge works only from the computer running it.

The AP setup password, sensor wiring, ESP32 strapping pins, brownout behavior and camera flow need physical validation before a public demonstration. In particular, HC-SR04 Echo is 5 V and must be divided to ESP32-safe voltage. A successful flash does not verify camera, Wi-Fi, speech or sensor operation.

If Chrome shows the mic as active but does not receive speech on the device's plain-HTTP IP page, open the dashboard through the desktop companion's `localhost` address or use the Android wrapper. The companion's `localhost` page is a secure context; the direct device IP page may not be one.

## Cloud assistant and cross-device memory (Supabase)

The competition project **ALFRED-Showcase** is deployed in Mumbai. Its URL and publishable key are prefilled in the firmware dashboard; the publishable key is intended for browser use. Supabase stores per-account conversation history and explicitly saved facts. The Edge Function is deployed and checks signed-in users itself. The two Gemini keys supplied by the project owner are configured as the `GEMINI_API_KEYS` Edge Function secret; they are not in the firmware or source package.

1. Open the ALFRED dashboard and go to **Settings → Cloud assistant & memory**. The project connection is prefilled.
2. Create an account or sign in on each device with the same email/password. If email confirmation is enabled, confirm the message before signing in. AI requests use the shared server key pool first; you may add your own Gemini keys under **Gemini API keys** as a fallback. The function tries the next key when Gemini reports an exhausted/revoked key. Personal keys stay in page memory by default; browser persistence is optional and uses unencrypted local storage.
3. To rotate or replace the shared keys, update `GEMINI_API_KEYS` in Supabase **Edge Functions → Secrets** as a comma- or newline-separated list in priority order. Keys belong in Supabase secrets, never in Git, firmware or an APK. Do not share an admin/service-role key.
4. Say “Remember that …” to save a durable fact. “Review saved memories” shows saved facts; “Clear cloud history & memory” deletes this user's conversations and memories.

Current-information requests invoke Gemini Google Search grounding when enabled and detected. It is real web grounding, not a cached answer. Google's current API pricing lists Search grounding as unavailable on the Free tier; an eligible paid-tier project is required. Search has its own monthly request allowance and usage charges after that allowance. If grounding is rejected, ALFRED should say it cannot verify current facts rather than pretend it searched.

The SQL migration and base `alfred-assistant` function are deployed on project `rviwomejtiiezezofmpn`; the new neural-voice function source must also be deployed before cloud voice is available. The daily cloud limit is 120 requests per account. For confirmation emails to work for competition accounts, configure Supabase Auth email delivery (custom SMTP) or use accounts that can receive the project's current confirmation email.

The function authenticates every request against Supabase Auth and scopes all database reads/writes to that verified user ID. The migration intentionally gives no direct table access to browser roles. The function gateway's legacy JWT check is disabled so the function can verify Supabase user tokens itself. Conversation text is saved to cloud for cross-device continuity; images are not saved, though a camera image attached to a request is sent to Gemini during that request. Users can clear stored history and facts in Settings.

## API key rotation

Server keys are tried first. On provider key rejection, permission errors or quota/rate-limit responses, the Edge Function tries the next configured key. It stops on request/model errors that a new key cannot fix. A single key cannot bypass a quota shared by its Google project. The per-account cloud limit still applies when using your own key; sign out and use local mode for requests that should not use ALFRED cloud services. Use keys from separately managed projects only when those projects and billing limits are legitimate and controlled by the project owner.

## GitHub

This local project is initialized as a Git repository but has no local remote configured. The private GitHub repository is managed through the connected GitHub account; publish source and documentation only, never API keys, Supabase admin credentials or build outputs.
