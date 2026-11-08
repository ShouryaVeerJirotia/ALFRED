# ALFRED assistant roadmap

ALFRED's goal is a fast, conversational assistant that works across phones and computers, with useful voice, memory, vision and opted-in proactive help. The ESP32-CAM remains a local camera/sensor device; account, AI and reminder services belong in the phone/web app and backend.

## Current baseline

- The ESP32-CAM firmware serves the responsive dashboard, camera snapshots, sensor readings, Wi-Fi provisioning, voice hooks and phone-action intents.
- Browser voice is user-started and browser-dependent. Android has a visible microphone foreground service that recognizes “Alfred,” then listens for a follow-up; with the activity closed it can queue the command for processing when ALFRED is reopened. This uses Android's installed speech-recognition service, not offline system-level hotword detection, and still needs phone verification with Android 1.4.0 and firmware v2.5.
- The ALFRED-Showcase Supabase project is deployed with private per-user conversation/memory tables, a daily quota and the `alfred-assistant` function. Two Gemini keys are held only in the `GEMINI_API_KEYS` Supabase secret; the function rotates across keys after provider quota/auth failures.
- The current function saves text conversation history and explicit memories. It can restore recent turns across devices. Camera frames are sent to Gemini for the request but are not stored in Supabase.
- The latest firmware compiled for the AI Thinker ESP32-CAM and was flash-verified. A normal boot and camera/sensor run still need a physical check. A real signed-in cloud chat/memory session also remains to be exercised.
- Current-information phrases can trigger Gemini Google Search grounding by default in a new browser profile. The user can turn it off in Settings. Google's current Gemini API pricing page lists Search grounding as unavailable on the Free tier. Cloud source-link extraction has been added to the local Edge Function but needs deployment before cloud answers return citations.
- A local desktop companion is now present. It proxies the ESP32 page on `localhost` and sends explicit coding tasks to the installed Codex CLI in a separate source snapshot. ALFRED displays the resulting patch and applies it only after a separate user click. End-to-end companion execution and patch application still need verification.
- The private GitHub repository has the standalone firmware and the cloud-ready source bundle. Provider keys and the original password-bearing backup are excluded.

## Phases and acceptance criteria

### 1. Verify the deployed core

- Boot normally after removing GPIO0 from GND; verify camera, dashboard, sensors, saved Wi-Fi and recovery AP.
- Create one ALFRED account, sign in on two devices, send chat, explicitly save a fact, review it, clear it, and confirm the same account sees the same memory on both devices.
- Test the shared key pool and one user-provided key without logging or displaying either key.
- Measure response time and show a clear offline/timeout/quota status.

### 2. Make voice dependable across devices

- Move the dashboard into a responsive HTTPS web app/PWA that can securely request microphone permission and connect to the local ALFRED device.
- Verify the implemented English/Hindi input/output selection on browsers and Android phones, keep push-to-talk and typed chat as accessible fallbacks, then add interruption controls.
- Verify the native Android foreground service with a visible indicator, user-controlled start/stop, battery controls and wake-to-follow-up capture. Browser-only recognition cannot promise this behavior.
- Test foreground, background, screen lock, revoked permissions, noisy rooms and speech-service outages on each target browser/OS.

### 3. Improve natural chat, speed and memory

- Stream model output so speech and text can begin before the full response is ready.
- Keep recent conversation context bounded; retrieve only relevant user-approved memories and offer clear review, correction, export and deletion controls.
- Give ALFRED a consistent concise, warm personality and clear uncertainty. Support Hindi and English without claiming live facts unless grounding is available.
- Confirm provider model names, search-grounding availability, latency and quotas before a competition build.

### 4. Add practical vision tools

- Add user-initiated OCR with read-aloud and adjustable detail.
- Add QR/barcode reading and a clear object-finding mode.
- Keep capture visible and user-controlled; explain when camera frames go to Gemini and do not retain them by default.
- Describe uncertainty and distance limits plainly; never present camera or ultrasonic output as safe navigation.

### 5. Add reminders and trusted help

- Implement reminders in the phone app first so they survive ESP32 resets and browser closure; add notification permission, time zone, repeat, quiet hours, edit, dismiss and delete flows.
- Use phone GPS and a preselected trusted-contact list for SOS, with explicit confirmation before sending location. Show delivery status and cancellation.
- Test denied permissions, no network, low battery, stale location and duplicate notifications.

### 6. Expand desktop coding and control safely

- The first local Codex bridge is implemented for one Git project selected when the companion starts. It snapshots visible project files, excludes secrets and generated folders, and uses Codex CLI with the workspace-write sandbox.
- Test a spoken code task, no-change result, successful patch preview/application, stale-patch rejection, discard, Codex failure and companion shutdown. Verify the original workspace stays untouched before “Apply patch”.
- Extend beyond code editing only through separately scoped local tools with a clear preview and approval for each action. Never let ESP32 firmware execute arbitrary computer commands.
- For coding requests from another device, add authenticated pairing and revocation; the current companion accepts only the same computer's localhost page.

### 7. Competition polish

- Build one reliable setup journey, status/error language and accessible Settings page for phones and PCs.
- Run a repeatable test matrix and record which features were demonstrated on hardware versus simulated or unavailable.
- Validate the workflow with blind/low-vision users before making broad accessibility claims.

## Current limits to keep visible

The ESP32 dashboard is served over local-network HTTP and has no device-level account sign-in. Voice recognition depends on browser support and page state. The local desktop companion can provide a `localhost` page, but phone-to-computer pairing is not implemented. The deployed cloud backend needs a user account and a normal signed-in request before its end-to-end memory path can be called validated. Reminders and general app/mouse control are not implemented. ALFRED is supplementary information only, never a replacement for a white cane, guide dog or another established mobility aid.
