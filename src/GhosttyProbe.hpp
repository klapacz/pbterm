#pragma once

// Minimal proof-of-integration for libghostty-vt.
// This intentionally does not replace pbterm's renderer yet; it only verifies
// that the PocketBook app can include, link, and call the Ghostty VT C API.
bool ghostty_probe_link();
