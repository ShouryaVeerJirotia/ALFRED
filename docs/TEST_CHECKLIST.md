# ALFRED verification checklist

## Firmware build and flash

- [x] Compile firmware v2.5 for AI Thinker ESP32-CAM with ESP32 Arduino core 3.3.10.
- [x] Flash firmware v2.5 through CP2102 and verify the application hash. The serial monitor was opened after reset, so normal startup was not captured.
- [ ] Confirm dashboard loading after v2.5 flash. The v2.4 normal boot emitted repeated camera `FB-OVF` warnings; `/status` could not be reached from this computer.
- [ ] Test power cycling, lost router, wrong password, setup AP and successful new Wi-Fi save.

## Browser/device support

- [ ] Android Chrome; iPhone/iPad Safari; Windows Chrome/Edge; macOS Safari/Chrome.
- [ ] Responsive layout, screen reader labels, keyboard operation, camera fetch and sensor data.
- [ ] Typed request and TTS on every target; microphone recognition only where target browser supports it.
- [ ] Install Android 1.4.0, then verify the “Alfred” wake followed by a spoken command while the app is open and backgrounded. Firmware v2.5 has been flashed; camera/Wi-Fi startup still needs verification.
- [x] Hindi, English (India), and automatic Hindi/English selector added; speech-service behavior remains device-dependent and unverified.
- [ ] Install the Android APK on a physical phone and test app startup, dashboard URL save, microphone/web permissions and Hindi/English settings.
- [x] Assemble the Android 1.4.0 debug APK. It still needs an Android phone for install and runtime checks.
- [ ] Android contact picker returns name/number without broad contacts permission; direct calls work after `CALL_PHONE` permission and AI calls require ALFRED confirmation.
- [ ] Test Spotify, YouTube Music and Any app with installed apps/accounts; confirm play-from-search autoplays a match where supported and clearly falls back to provider search otherwise.
- [ ] Test hands-free notification Stop action, wake phrase plus follow-up, silence/no-network/missing speech service, phone lock and returning from calls/contacts.
- [ ] Compare Gemini neural voice across Android and desktop. Test TTS quota/network fallback and confirm the device voice fallback remains understandable.

## Product functions

- [x] Supabase memory/quota migration and `alfred-assistant` Edge Function deployed; `GEMINI_API_KEYS` configured as a Supabase secret.
- [x] Latest Edge Function deployment is visible in the Supabase dashboard; it includes live web excerpts and Gemini summarization as fallback from Google Search grounding. No live request was made as part of deployment verification.
- [ ] Signed-in Gemini chat, automatic Google Search grounding for current-information prompts, source links, image description and key rotation after a rejected/exhausted key.
- [x] Deploy the latest Edge Function. Same-account memory, web-result summarization, and spoken answer still need live signed-in request tests.
- [ ] Check Search grounding eligibility, account quota and charges before the showcase; the cloud web-results fallback should be checked with a live account/key.
- [ ] OCR/read-aloud and object-finding actions: controls and prompts are implemented locally; camera and result quality are not verified.
- [ ] Invalid key/model/network errors are shown accurately and do not loop.
- [ ] Supabase sign-up/sign-in, session refresh, cross-device chat history, explicit memory, review and delete.
- [ ] Confirm browser voice remains user-started; Android hands-free needs a visible microphone foreground service and network-backed recognizer, not an offline wake engine.
- [ ] Reminder creation, persistence, notification delivery, quiet hours and cancellation.
- [ ] Run the desktop companion against a test Git project; verify localhost audio input, isolated Codex edits, secret-file exclusions, patch preview, apply, discard and stale-patch rejection.
- [ ] Verify same-computer-only access, Codex authentication failure, device offline and companion shutdown behavior; phone-to-PC pairing remains a separate future feature.
- [ ] Call/SMS confirmation, Android direct call and contact lookup, music provider routing, and navigation link on real hardware.
- [ ] Camera absent, sensor absent/stale and no-network states remain understandable.

## Physical and accessibility validation

- [ ] Stable power under camera/Wi-Fi load; camera ribbon secure; no brownout/reboot.
- [ ] HC-SR04 Echo voltage divided to 3.3 V; check boot-strapping-pin effects on GPIO12/15.
- [ ] Sensor measurements compared to known references; flame sensor tested only as a hobby sensor.
- [ ] Blind/low-vision user review of setup, voice feedback, screen reader flow, errors and confirmations.
- [ ] Clearly state that ALFRED is supplementary and cannot guarantee safe navigation.
