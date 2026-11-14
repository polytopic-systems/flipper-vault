"""Compile and run the host-native protocol regression binaries.

The script builds the CTAP/PIN/store regression suite (dev CTAP 2.1 and release
configurations) and the USB/NFC transport suite with the same fake Flipper headers
used by CI, then executes the binaries. Legacy U2F (CTAP1) is not supported; the
suites check that CTAP1 requests are rejected on every transport.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NATIVE_SOURCE = ROOT / "tests" / "native" / "protocol" / "runner.c"
NATIVE_TRANSPORT_SOURCE = ROOT / "tests" / "native" / "transport" / "runner.c"
NATIVE_CRYPTO_SOURCE = ROOT / "tests" / "native" / "crypto" / "runner.c"
NATIVE_BLE_SOURCE = ROOT / "tests" / "native" / "ble" / "runner.c"
NATIVE_INCLUDE = ROOT / "tests" / "native" / "include"
NATIVE_BINARY = ROOT / ".tmp" / "native_protocol_regressions"
NATIVE_PACKED_OFF_BINARY = ROOT / ".tmp" / "native_protocol_regressions_packed_off"
NATIVE_BLE_PROTOCOL_BINARY = ROOT / ".tmp" / "native_protocol_regressions_ble"
NATIVE_RELEASE_PROTOCOL_BINARY = ROOT / ".tmp" / "native_protocol_regressions_release"
NATIVE_TRANSPORT_BINARY = ROOT / ".tmp" / "native_transport_regressions"
NATIVE_CRYPTO_BINARY = ROOT / ".tmp" / "native_crypto_regressions"
NATIVE_BLE_BINARY = ROOT / ".tmp" / "native_ble_framing_regressions"
POLICY_SOURCE = ROOT / "src" / "ctap" / "policy.c"


def run(cmd: list[str]) -> None:
    """Echo and run one compiler or test command in the repository root."""
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> None:
    """Build the native regression binaries and fail on the first error."""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise SystemExit("missing host C compiler")

    policy = POLICY_SOURCE.read_text()
    if "zf_ctap_request_uses_allow_list" not in policy:
        raise SystemExit("policy is missing allowList semantics helper")

    NATIVE_BINARY.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src" / "crypto"),
            str(NATIVE_CRYPTO_SOURCE),
            str(ROOT / "src" / "crypto" / "aes256.c"),
            "-o",
            str(NATIVE_CRYPTO_BINARY),
        ]
    )
    run([str(NATIVE_CRYPTO_BINARY)])
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src"),
            str(NATIVE_BLE_SOURCE),
            "-o",
            str(NATIVE_BLE_BINARY),
        ]
    )
    run([str(NATIVE_BLE_BINARY)])
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DZF_HOST_TEST",
            "-DZF_RELEASE_DIAGNOSTICS=1",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src"),
            str(NATIVE_SOURCE),
            "-o",
            str(NATIVE_BINARY),
        ]
    )
    run([str(NATIVE_BINARY)])
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DZF_HOST_TEST",
            "-DZF_RELEASE_DIAGNOSTICS=1",
            "-DZF_PACKED_ATTESTATION=0",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src"),
            str(NATIVE_SOURCE),
            "-o",
            str(NATIVE_PACKED_OFF_BINARY),
        ]
    )
    run([str(NATIVE_PACKED_OFF_BINARY)])
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DZF_HOST_TEST",
            "-DZF_RELEASE_DIAGNOSTICS=1",
            "-DZF_BLE_ENABLED=1",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src"),
            str(NATIVE_SOURCE),
            "-o",
            str(NATIVE_BLE_PROTOCOL_BINARY),
        ]
    )
    run([str(NATIVE_BLE_PROTOCOL_BINARY)])
    # Release configuration: CTAP 2.1 paths compiled out (ZF_DEV_FIDO2_1=0). Only the
    # release checks run here, so the rest of the suite is legitimately unused.
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-DZF_HOST_TEST",
            "-DZF_RELEASE_DIAGNOSTICS=1",
            "-DZF_DEV_FIDO2_1=0",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src"),
            str(NATIVE_SOURCE),
            "-o",
            str(NATIVE_RELEASE_PROTOCOL_BINARY),
        ]
    )
    run([str(NATIVE_RELEASE_PROTOCOL_BINARY)])
    run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DZF_HOST_TEST",
            "-DZF_RELEASE_DIAGNOSTICS=1",
            "-I",
            str(NATIVE_INCLUDE),
            "-I",
            str(ROOT / "src"),
            str(NATIVE_TRANSPORT_SOURCE),
            "-o",
            str(NATIVE_TRANSPORT_BINARY),
        ]
    )
    run([str(NATIVE_TRANSPORT_BINARY)])


if __name__ == "__main__":
    main()
