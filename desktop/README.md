# ALFRED desktop companion

The companion opens the ESP32 dashboard on `localhost` and adds an optional Codex coding panel. It requires Python 3, Git, the Codex CLI installed and signed in, and a Git project to select.

From the ALFRED project folder, run:

```sh
python3 desktop/alfred_desktop.py \
  --device-url http://alfred.local \
  --workspace /absolute/path/to/your/project
```

If `alfred.local` does not resolve on the computer, replace it with ALFRED's current private Wi-Fi IP. The computer and ESP32-CAM need to share the same network. Open `http://127.0.0.1:8765` on that computer; keep the terminal running while you use the page.

## What a code request does

1. ALFRED asks before sending the request to Codex.
2. The companion copies visible project files into a temporary directory. It skips `.env` files, files with secret/credential/token/key names, private-key formats, files containing common provider-key/private-key patterns, and common generated folders.
3. Codex works only in that isolated Git copy with the `workspace-write` sandbox. The companion does not pass API-key environment variables to Codex.
4. ALFRED displays the patch. “Apply patch to project” checks it against the current project and then applies it; “Discard patch” deletes the temporary copy.

The companion binds to `127.0.0.1`; it is not a network service for a phone or another computer. It does not control the mouse or other desktop apps, and it does not commit, upload, or deploy. It will reject a patch if the project changed in a way that makes the patch no longer apply cleanly.

Codex tasks may still fail if the CLI is not signed in, the selected project is too large, or Codex requests a permission the non-interactive bridge cannot grant. No task is run automatically after listening; the user confirms it, then separately approves applying its patch.
