#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Release/update contract gate for CarService PointG.

Checks the updater-critical contract across CMake, Inno Setup, GitHub manifest,
upload instructions and updater source.  By default it validates source-tree
consistency.  With --final it additionally requires the built installer hash to
be materialized in github_release_manifest.json and Output/SHA256SUMS.txt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def fail(errors: list[str], msg: str) -> None:
    errors.append(msg)


def normalize_version(v: str) -> str:
    v = v.strip()
    if v.lower().startswith("v"):
        v = v[1:]
    v = re.sub(r"[^0-9.]", "", v).strip(".")
    return v


def version_token_matches(name: str, version: str) -> bool:
    low = name.lower()
    norm = normalize_version(version)
    variants = {norm, norm.replace(".", "_"), norm.replace(".", "-")}
    for token in variants:
        pattern = rf"(^|[^0-9a-z])v?{re.escape(token.lower())}(?![._-]?[0-9])(?=$|[^0-9a-z])"
        if re.search(pattern, low, flags=re.IGNORECASE):
            return True
    return False


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def checksum_matches(path: Path, name: str, expected: str) -> bool:
    matches = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"([a-fA-F0-9]{64})[ \t]+\*?(.+)", line)
        if not match:
            return False
        if match.group(2) == name:
            matches.append(match.group(1).lower())
    return matches == [expected]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--final", action="store_true", help="require generated installer SHA256 release assets")
    args = parser.parse_args(argv)

    errors: list[str] = []
    warnings: list[str] = []

    cmake = read("CMakeLists.txt")
    manifest = json.loads(read("github_release_manifest.json"))
    iss = read("CarService_PointG_v2_1.iss")
    upload = read("GITHUB_RELEASE_UPLOAD_LIST_2.1.txt")
    updater_parts = [read("src/app/update/UpdateServiceSupport.cpp")]
    for fragment in sorted((ROOT / "src/app/update/internal").glob("Update*.ipp")):
        updater_parts.append(fragment.read_text(encoding="utf-8", errors="replace"))
    updater = "\n".join(updater_parts)
    build_ps = read("scripts/build_release_2_1.ps1")
    make_assets_ps = read("scripts/make_github_release_assets_v21.ps1")

    cmake_version_match = re.search(r"project\(CarService\s+VERSION\s+([0-9.]+)", cmake)
    if not cmake_version_match:
        fail(errors, "CMake project(CarService VERSION ...) not found")
        cmake_version = ""
    else:
        cmake_version = cmake_version_match.group(1)

    manifest_version = str(manifest.get("version", ""))
    tag_name = str(manifest.get("tag_name", ""))
    installer_asset = manifest.get("auto_update_contract", {}).get("installer_asset_name", "")
    output_base = manifest.get("installer", {}).get("output_base_filename", "")

    if cmake_version != manifest_version:
        fail(errors, f"CMake version {cmake_version!r} != manifest version {manifest_version!r}")
    if normalize_version(tag_name) != manifest_version:
        fail(errors, f"manifest tag_name {tag_name!r} does not match version {manifest_version!r}")

    iss_version_match = re.search(r"#define\s+MyAppVersion\s+\"([^\"]+)\"", iss)
    if not iss_version_match:
        fail(errors, "ISS #define MyAppVersion not found")
    elif iss_version_match.group(1) != manifest_version:
        fail(errors, f"ISS MyAppVersion {iss_version_match.group(1)!r} != manifest version {manifest_version!r}")

    if f"CarServicePointG_Setup_x86_x64_v{manifest_version}" not in iss:
        fail(errors, "ISS OutputBaseFilename does not contain canonical setup base name")
    if output_base != f"CarServicePointG_Setup_x86_x64_v{manifest_version}":
        fail(errors, f"manifest installer.output_base_filename is not canonical: {output_base!r}")
    if installer_asset != f"CarServicePointG_Setup_x86_x64_v{manifest_version}.exe":
        fail(errors, f"manifest installer asset is not canonical: {installer_asset!r}")

    lowered_upload = upload.lower()
    forbidden_hotfix_phrases = [
        "same-version hotfix is allowed",
        "asset updated_at is newer",
        "updated_at is newer than the installed exe timestamp",
    ]
    for phrase in forbidden_hotfix_phrases:
        if phrase in lowered_upload:
            fail(errors, f"Upload instructions still allow forbidden same-version hotfix rule: {phrase}")
    if "same-version hotfix is prohibited" not in lowered_upload and "same-version hotfix заборонено" not in lowered_upload:
        fail(errors, "Upload instructions do not explicitly prohibit same-version hotfix")

    contract = manifest.get("auto_update_contract", {})
    if contract.get("authenticode_signature_required_in_production") is not True:
        fail(errors, "manifest must require production Authenticode signatures")
    if contract.get("unsigned_dev_override_env") != []:
        fail(errors, "manifest must not advertise unsupported unsigned update overrides")
    if contract.get("same_version_hotfix") is not False:
        fail(errors, "manifest auto_update_contract.same_version_hotfix must be false")
    if contract.get("asset_updated_at_must_not_be_used_as_hotfix_trigger") is not True:
        fail(errors, "manifest must forbid asset updated_at as a hotfix trigger")

    # Asset-token boundary tests must match updater behavior.
    positive = [f"CarServicePointG_Setup_x86_x64_v{manifest_version}.exe", f"setup-{manifest_version}.exe"]
    wrong_version = manifest_version + ".999"
    next_parts = manifest_version.split(".")
    next_parts[-1] = str(int(next_parts[-1]) + 1)
    next_version = ".".join(next_parts)
    negative = [f"CarServicePointG_Setup_x86_x64_v{wrong_version}.exe", f"setup-{next_version}.exe", f"vc_redist_x64_v{manifest_version}.exe", f"helper_debug_v{manifest_version}.exe"]
    for name in positive:
        if not version_token_matches(name, manifest_version):
            fail(errors, f"asset version-token matcher rejected valid name: {name}")
    for name in negative[:2]:
        if version_token_matches(name, manifest_version):
            fail(errors, f"asset version-token matcher accepts wrong version name: {name}")

    required_updater_markers = [
        "assetNameMatchesVersionToken",
        "isCanonicalInstallerAssetName",
        "update_lock_state.json",
        "update_transaction.json",
        "failed_update_packages.json",
        "update_once_global.lock",
        "Global\\\\CarServicePointG.Update.Once",
        "processExistsByPid",
        "stale update lock was removed because app pid is not active",
        "updater_pid",
        "installer_pid",
        "heartbeat_utc",
        "asset_name",
        "expected_version",
        "asset_sha256",
        "apply_update_*.log",
        "update already running",
        "UPDATE_SKIP_FAILED_PACKAGE_COOLDOWN",
        "UPDATE_SKIP_ALREADY_APPLIED_PACKAGE",
        "System32/WindowsPowerShell/v1.0/powershell.exe",
        "Write-Status",
        "Set-UpdateTransactionState",
        "Update-HandoffHeartbeat",
        "Add-FailedPackageMarker",
        "Release-UpdateOnceMutex",
        "LatestStatusPath",
        "app_shutdown_stage_timeout",
        "handoff already owns transaction",
        "Shortcut preserved, not owned by current AppDir",
        "OLD_VERSION_CLEANUP skip publisher mismatch",
    ]
    for marker in required_updater_markers:
        if marker not in updater:
            fail(errors, f"updater source missing required marker: {marker}")

    forbidden_updater_shortcut_patterns = ["CarService PointG *.lnk", "CarServicePointG*.lnk", "CarService*.lnk"]
    for pattern in forbidden_updater_shortcut_patterns:
        if pattern in updater:
            fail(errors, f"updater still uses broad shortcut cleanup pattern: {pattern}")

    if "check_update_release_contract.py" not in cmake:
        fail(errors, "CMake does not register update_release_contract_gate")
    if "check_update_release_contract.py" not in build_ps:
        fail(errors, "build_release_2_1.ps1 does not run update release contract gate")
    if "asset_sha256" not in make_assets_ps or "github_release_manifest.json" not in make_assets_ps:
        fail(errors, "make_github_release_assets_v21.ps1 does not update manifest asset_sha256")

    if not (ROOT / "tests" / "update" / "UpdateReleaseContractTests.cpp").exists():
        fail(errors, "update release contract Qt test is missing")
    elif "UpdateReleaseContractTests" not in read("cmake/modules/CarServiceTests.cmake"):
        fail(errors, "UpdateReleaseContractTests is not registered in CarServiceTests.cmake")

    if not (ROOT / "tests" / "update" / "UpdateOnceGuaranteeTests.cpp").exists():
        fail(errors, "update once guarantee Qt test is missing")
    elif "UpdateOnceGuaranteeTests" not in read("cmake/modules/CarServiceTests.cmake"):
        fail(errors, "UpdateOnceGuaranteeTests is not registered in CarServiceTests.cmake")

    sha_value = str(manifest.get("asset_sha256", ""))
    if args.final:
        if "TO_BE_FILLED" in sha_value or not re.fullmatch(r"[a-f0-9]{64}", sha_value):
            fail(errors, "final manifest asset_sha256 is not materialized")
        installer_path = ROOT / "Output" / installer_asset
        sums_path = ROOT / "Output" / "SHA256SUMS.txt"
        sha_path = ROOT / "Output" / f"{installer_asset}.sha256"
        for path in (installer_path, sums_path, sha_path):
            if not path.exists():
                fail(errors, f"final release asset missing: {path.relative_to(ROOT)}")
        if installer_path.exists():
            actual = sha256_file(installer_path)
            if actual != sha_value:
                fail(errors, "final manifest asset_sha256 does not match installer")
            for checksum_path in (sums_path, sha_path):
                if checksum_path.exists() and not checksum_matches(checksum_path, installer_asset, actual):
                    fail(errors, f"invalid or mismatched checksum asset: {checksum_path.name}")
    elif not sha_value or "TO_BE_FILLED" in sha_value:
        warnings.append("manifest asset_sha256 is still a build-time placeholder; --final gate must run after asset generation")

    if errors:
        print("UPDATE_RELEASE_CONTRACT_GATE FAILED")
        for e in errors:
            print("ERROR:", e)
        for w in warnings:
            print("WARNING:", w)
        return 1

    print("UPDATE_RELEASE_CONTRACT_GATE OK")
    for w in warnings:
        print("WARNING:", w)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
