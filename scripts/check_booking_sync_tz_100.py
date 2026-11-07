#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    p = ROOT / rel
    if not p.exists():
        raise SystemExit(f"[booking_sync_tz_gate] ERROR missing file: {rel}")
    return p.read_text(encoding="utf-8", errors="ignore")

checks = [
    ("src/core/services/booking/BookingSyncManager.cpp", "GET", "/api/booking/sync"),
    ("src/core/services/booking/BookingSyncManager.cpp", "PATCH", "/api/booking/"),
    ("src/core/services/booking/PlanningAvailabilityService.cpp", "planning busy guard", "У цей час у Плануванні вже немає вільного поста/майстра"),
    ("src/core/services/booking/PlanningAvailabilityService.cpp", "long repair guard", "День заблокований довгостроковим ремонтом"),
    ("src/core/services/booking/PlanningAvailabilityService.cpp", "Planning insert", "addOnlineBookingToPlanning"),
    ("src/core/services/booking/BookingRepository.cpp", "online_bookings table", "CREATE TABLE IF NOT EXISTS online_bookings"),
    ("src/ui/main/MainWindow.cpp", "admin modal", "Новий онлайн-запис"),
    ("src/ui/main/MainWindow.cpp", "open planning", "Відкрити в Плануванні"),
    ("src/ui/main/MainWindow.cpp", "create order", "Створити наряд"),
    ("src/ui/tabs/schedule/PlanningPage.cpp", "planning date opener", "openOnlineBookingDate"),
    ("src/ui/tabs/settings/SettingsIntegrationsSection.cpp", "settings UI", "Онлайн-запис із сайту"),
    ("src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp", "startup schema", "online_bookings"),
]

errors = []
for rel, label, marker in checks:
    if marker not in read(rel):
        errors.append(f"{rel}: missing {label} marker {marker!r}")

cmake = read("cmake/modules/CarServiceSourcesCore.cmake")
for rel in [
    "src/core/services/booking/BookingRecord.cpp",
    "src/core/services/booking/BookingSettings.cpp",
    "src/core/services/booking/BookingRepository.cpp",
    "src/core/services/booking/PlanningAvailabilityService.cpp",
    "src/core/services/booking/BookingSyncManager.cpp",
]:
    if rel not in cmake:
        errors.append(f"CMake missing source {rel}")

if errors:
    print("[booking_sync_tz_gate] ERROR")
    for e in errors:
        print(" -", e)
    raise SystemExit(1)
print("[booking_sync_tz_gate] OK: booking API sync, planning insertion, admin modal and settings wiring are present")
