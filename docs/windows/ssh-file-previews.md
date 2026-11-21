# SSH file previews on Windows

The terminal discovers an OpenSSH process in its process tree. Hover previews
use a separate SSH connection with the original destination, port, user,
identity, configuration and jump-host options. Files need not have an extension.
The remote host currently needs a POSIX shell and GNU stat, head, base64 and file.

Key or agent authentication runs without a separate window. If authentication
or host-key verification requires interaction, Konsole retries in a visible
OpenSSH console. Enter the password or verify the host fingerprint there.
Passwords are handled by OpenSSH and are not stored by Konsole.

A plain ls listing does not contain its absolute directory. With OSC 7 prompt
integration Konsole resolves relative names automatically. Without OSC 7 it asks
for an absolute directory on the first preview. After changing directories,
right-click a filename and choose “Set Remote Preview Directory…” to update it.
Absolute file paths do not require this setting.

Example for a remote Bash prompt (run on the remote host):

```bash
__konsole_osc7() { printf '\033]7;file://%s%s\007' "$HOSTNAME" "$PWD"; }
PROMPT_COMMAND="__konsole_osc7${PROMPT_COMMAND:+; $PROMPT_COMMAND}"
```

This is a local source change. The Windows-specific process discovery and
interactive authentication paths still require a Windows build and runtime
verification; the standalone Linux preview tests do not cover those paths.
