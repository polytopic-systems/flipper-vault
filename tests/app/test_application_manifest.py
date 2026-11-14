"""Tests for build-profile defaults in application.fam."""

from __future__ import annotations

import runpy
import unittest
from unittest import mock

from tests.harness import ROOT

MANIFEST = ROOT / "application.fam"


class ManifestProfileTests(unittest.TestCase):
    def load_manifest(self, env: dict[str, str] | None = None) -> dict[str, object]:
        captured: dict[str, object] = {}

        def app_stub(**kwargs: object) -> None:
            captured.update(kwargs)

        class AppTypeStub:
            EXTERNAL = "external"

        with mock.patch.dict("os.environ", env or {}, clear=True):
            runpy.run_path(
                str(MANIFEST),
                init_globals={
                    "App": app_stub,
                    "FlipperAppType": AppTypeStub,
                },
            )

        return captured

    def test_default_build_is_full_release_safe(self) -> None:
        app = self.load_manifest()

        self.assertEqual(app["appid"], "flipper_vault")
        self.assertEqual(app["name"], "Vault")
        self.assertEqual(app["fap_description"], "FIDO2 passkey vault")
        self.assertEqual(app["fap_author"], "Alex Stoyanov")
        self.assertEqual(app["fap_version"], (0, 8))
        self.assertNotIn("ZF_USB_ONLY", app["cdefines"])
        self.assertNotIn("ZF_NFC_ONLY", app["cdefines"])
        self.assertIn("ZF_BLE_ENABLED=1", app["cdefines"])
        self.assertIn("ZF_DEV_DEFAULT_BLE=0", app["cdefines"])
        self.assertIn("ZF_RELEASE_DIAGNOSTICS=0", app["cdefines"])
        self.assertIn("ZF_USB_DIAGNOSTICS=0", app["cdefines"])
        self.assertIn("ZF_AUTO_ACCEPT_REQUESTS=0", app["cdefines"])
        self.assertIn("ZF_DEV_SCREENSHOT=0", app["cdefines"])
        self.assertIn("ZF_DEV_FIDO2_1=0", app["cdefines"])
        self.assertIn("ZF_PACKED_ATTESTATION=1", app["cdefines"])
        self.assertNotIn("!nfc_*.c", app["sources"])
        self.assertNotIn("!usb_hid_*.c", app["sources"])
        self.assertNotIn("!ble_*.c", app["sources"])
        self.assertIn("!nfc_trace.c", app["sources"])
        self.assertIn("!zerofido_usb_diagnostics.c", app["sources"])
        self.assertIn("nfc", app["requires"])
        self.assertIn("bt", app["requires"])
        self.assertIn("!.tmp", app["sources"])
        self.assertIn("!.venv", app["sources"])
        self.assertIn("!*_debug.c", app["sources"])
        self.assertIn("!debug_*.c", app["sources"])
        self.assertIn("!*_test.c", app["sources"])

    def test_usb_profile_is_usb_only(self) -> None:
        app = self.load_manifest({"ZEROFIDO_PROFILE": "usb"})

        self.assertIn("ZF_USB_ONLY", app["cdefines"])
        self.assertIn("ZF_BLE_ENABLED=0", app["cdefines"])
        self.assertIn("!nfc_*.c", app["sources"])
        self.assertIn("!ble_*.c", app["sources"])
        self.assertNotIn("nfc", app["requires"])
        self.assertNotIn("bt", app["requires"])

    def test_nfc_profile_is_nfc_only(self) -> None:
        app = self.load_manifest({"ZEROFIDO_PROFILE": "nfc"})

        self.assertIn("ZF_NFC_ONLY", app["cdefines"])
        self.assertIn("ZF_BLE_ENABLED=0", app["cdefines"])
        self.assertIn("!usb_hid_*.c", app["sources"])
        self.assertIn("!ble_*.c", app["sources"])
        self.assertIn("nfc", app["requires"])
        self.assertNotIn("bt", app["requires"])

    def test_full_profile_can_drop_bluetooth(self) -> None:
        app = self.load_manifest({"ZEROFIDO_PROFILE": "full", "ZEROFIDO_BLE": "0"})

        self.assertIn("ZF_BLE_ENABLED=0", app["cdefines"])
        self.assertIn("!ble_*.c", app["sources"])
        self.assertNotIn("bt", app["requires"])

    def test_dev_default_ble_flag(self) -> None:
        app = self.load_manifest({"VAULT_DEV_DEFAULT_BLE": "1"})

        self.assertIn("ZF_DEV_DEFAULT_BLE=1", app["cdefines"])
        with self.assertRaisesRegex(ValueError, "VAULT_DEV_DEFAULT_BLE"):
            self.load_manifest({"ZEROFIDO_PROFILE": "usb", "VAULT_DEV_DEFAULT_BLE": "1"})
        with self.assertRaisesRegex(ValueError, "ZEROFIDO_BLE"):
            self.load_manifest({"ZEROFIDO_PROFILE": "nfc", "ZEROFIDO_BLE": "1"})

    def test_development_flags_are_explicitly_configurable(self) -> None:
        app = self.load_manifest(
            {
                "ZEROFIDO_PROFILE": "usb",
                "ZEROFIDO_RELEASE_DIAGNOSTICS": "1",
                "ZEROFIDO_USB_DIAGNOSTICS": "true",
                "ZEROFIDO_AUTO_ACCEPT_REQUESTS": "on",
                "ZEROFIDO_DEV_SCREENSHOT": "yes",
                "ZEROFIDO_DEV_FIDO2_1": "1",
                "ZEROFIDO_PACKED_ATTESTATION": "false",
            }
        )

        self.assertIn("ZF_USB_ONLY", app["cdefines"])
        self.assertIn("ZF_RELEASE_DIAGNOSTICS=1", app["cdefines"])
        self.assertIn("ZF_USB_DIAGNOSTICS=1", app["cdefines"])
        self.assertIn("ZF_AUTO_ACCEPT_REQUESTS=1", app["cdefines"])
        self.assertIn("ZF_DEV_SCREENSHOT=1", app["cdefines"])
        self.assertIn("ZF_DEV_FIDO2_1=1", app["cdefines"])
        self.assertIn("ZF_PACKED_ATTESTATION=0", app["cdefines"])
        self.assertNotIn("!nfc_trace.c", app["sources"])
        self.assertNotIn("!zerofido_usb_diagnostics.c", app["sources"])

    def test_invalid_boolean_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "ZEROFIDO_RELEASE_DIAGNOSTICS"):
            self.load_manifest({"ZEROFIDO_RELEASE_DIAGNOSTICS": "maybe"})


if __name__ == "__main__":
    unittest.main()
