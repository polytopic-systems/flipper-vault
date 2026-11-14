"""Regression tests for reset preserving attestation identity."""

from __future__ import annotations

import re
import unittest

from tests.harness import ROOT


class ResetAttestationIdentityTests(unittest.TestCase):
    def test_reset_wipe_keeps_fido2_attestation_assets(self) -> None:
        """authenticatorReset wipes credentials and PIN, never the attestation key pair."""
        source = (ROOT / "src/store/bootstrap.c").read_text(encoding="utf-8")
        match = re.search(
            r"bool zf_store_bootstrap_wipe_app_data\(Storage \*storage\) \{(?P<body>.*?)\n\}",
            source,
            re.S,
        )

        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertNotIn("attestation", body)
        self.assertNotIn("cert", body)
        self.assertIn("client_pin", body)

    def test_reset_no_longer_touches_legacy_u2f_state(self) -> None:
        """Legacy U2F (CTAP1) was removed; reset must not depend on it."""
        source = (ROOT / "src/ctap/commands/reset.c").read_text(encoding="utf-8")

        self.assertNotIn("u2f", source.lower())
        self.assertFalse((ROOT / "src/u2f").exists())


if __name__ == "__main__":
    unittest.main()
