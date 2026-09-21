"""Compile every project-owned Blueprint asset in memory and emit a fail-closed receipt.

This runs through Content/Python/init_unreal.py via the existing
CODEX_RUN_MIGRATION_BASELINE_PIE hook.  It deliberately never calls a save API:
the editor process exits after the receipt is written, discarding any in-memory
compile dirtiness.  The receipt also detects on-disk Blueprint package changes.
"""

import hashlib
import json
import os
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()
ARTIFACTS_ROOT = (PROJECT_DIR / "Saved" / "Artifacts").resolve()
DEFAULT_REPORT = (
    ARTIFACTS_ROOT
    / "PendingValidation"
    / "BlueprintCompile"
    / "AllProjectBlueprintCompile.json"
)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def is_within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def requested_report_path():
    raw = os.environ.get("CODEX_ALL_PROJECT_BLUEPRINT_REPORT", "").strip()
    if not raw:
        return DEFAULT_REPORT, ""
    candidate = Path(raw).expanduser().resolve()
    if is_within(candidate, ARTIFACTS_ROOT):
        return candidate, ""
    return (
        DEFAULT_REPORT,
        "CODEX_ALL_PROJECT_BLUEPRINT_REPORT must resolve below Saved/Artifacts: "
        + str(candidate),
    )


REPORT, REPORT_PATH_ERROR = requested_report_path()


def safe_text(value):
    if value is None:
        return ""
    return str(value)


def asset_class_name(asset_data):
    try:
        return safe_text(asset_data.asset_class_path.asset_name)
    except Exception:
        return safe_text(getattr(asset_data, "asset_class", ""))


def package_name(asset_data):
    return safe_text(getattr(asset_data, "package_name", ""))


def normalized_blueprint_status(blueprint):
    raw = safe_text(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in raw.upper() if character.isalnum())
    return raw, normalized


def is_blueprint_asset_class(class_name):
    normalized = class_name.replace(" ", "").lower()
    return normalized == "blueprint" or normalized.endswith("blueprint")


def object_path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return safe_text(value)


def snapshot(path):
    if not path.is_file():
        return None
    item = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return {
        "path": str(path),
        "length": item.st_size,
        "mtime_ns": item.st_mtime_ns,
        "sha256": digest.hexdigest().upper(),
    }


def package_file_base(package, mount_content_roots):
    """Resolve a long package name to its uasset base without writing anything."""
    package_name_api = getattr(unreal, "PackageName", None)
    for method_name in (
        "try_convert_long_package_name_to_filename",
        "long_package_name_to_filename",
    ):
        method = getattr(package_name_api, method_name, None)
        if not callable(method):
            continue
        try:
            value = method(package, ".uasset")
            if isinstance(value, tuple):
                value = next(
                    (candidate for candidate in reversed(value) if isinstance(candidate, str)),
                    "",
                )
            filename = safe_text(value)
            if filename:
                path = Path(filename)
                if path.suffix.lower() == ".uasset":
                    return path.with_suffix("")
                return path
        except Exception:
            continue

    for mount_point, content_root in mount_content_roots.items():
        if package == mount_point or package.startswith(mount_point + "/"):
            relative = package[len(mount_point) :].lstrip("/")
            return content_root / relative
    return None


def package_file_snapshots(package, mount_content_roots):
    base = package_file_base(package, mount_content_roots)
    if base is None:
        return None
    return {
        suffix: snapshot(Path(str(base) + suffix))
        for suffix in (".uasset", ".uexp", ".ubulk", ".m.ubulk", ".uptnl")
    }


def discover_project_plugin_mounts(payload):
    """Return only mounted content roots owned by this target project's Plugins tree."""
    plugins_root = PROJECT_DIR / "Plugins"
    content_roots = {}
    descriptors = []
    plugin_library = getattr(unreal, "PluginBlueprintLibrary", None)
    mounted_api = getattr(plugin_library, "is_plugin_mounted", None)
    mount_api = getattr(plugin_library, "get_plugin_mounted_asset_path", None)

    if not plugins_root.is_dir():
        payload["plugin_mounts"] = descriptors
        return content_roots

    for descriptor_path in sorted(plugins_root.rglob("*.uplugin")):
        entry = {
            "plugin": descriptor_path.stem,
            "descriptor": str(descriptor_path.relative_to(PROJECT_DIR)).replace("\\", "/"),
            "can_contain_content": False,
            "content_directory": "",
            "mount_point": "",
            "result": "SKIPPED",
            "error": "",
        }
        try:
            descriptor = json.loads(descriptor_path.read_text(encoding="utf-8-sig"))
            entry["can_contain_content"] = bool(descriptor.get("CanContainContent", False))
        except Exception as exc:
            entry["result"] = "FAIL_DESCRIPTOR_READ"
            entry["error"] = repr(exc)
            payload["failures"].append(
                {
                    "reason": "PROJECT_PLUGIN_DESCRIPTOR_READ_FAILED",
                    "descriptor": entry["descriptor"],
                    "error": repr(exc),
                }
            )
            descriptors.append(entry)
            continue

        content_dir = descriptor_path.parent / "Content"
        entry["content_directory"] = str(content_dir.relative_to(PROJECT_DIR)).replace("\\", "/")
        if not entry["can_contain_content"] or not content_dir.is_dir():
            entry["result"] = "SKIPPED_NO_CONTENT"
            descriptors.append(entry)
            continue

        if not callable(mounted_api) or not callable(mount_api):
            entry["result"] = "FAIL_MOUNT_API_UNAVAILABLE"
            entry["error"] = (
                "PluginBlueprintLibrary.is_plugin_mounted or "
                "get_plugin_mounted_asset_path is unavailable"
            )
            payload["failures"].append(
                {
                    "reason": "PROJECT_PLUGIN_MOUNT_API_UNAVAILABLE",
                    "plugin": entry["plugin"],
                }
            )
            descriptors.append(entry)
            continue

        try:
            if not bool(mounted_api(entry["plugin"])):
                entry["result"] = "SKIPPED_NOT_MOUNTED"
                descriptors.append(entry)
                continue
            mount_value = mount_api(entry["plugin"])
            if isinstance(mount_value, tuple):
                mount_value = next(
                    (candidate for candidate in reversed(mount_value) if isinstance(candidate, str)),
                    "",
                )
            mount_point = safe_text(mount_value).rstrip("/")
        except Exception as exc:
            entry["result"] = "FAIL_MOUNT_RESOLUTION"
            entry["error"] = repr(exc)
            payload["failures"].append(
                {
                    "reason": "PROJECT_PLUGIN_MOUNT_RESOLUTION_FAILED",
                    "plugin": entry["plugin"],
                    "error": repr(exc),
                }
            )
            descriptors.append(entry)
            continue

        if not mount_point.startswith("/") or mount_point == "/":
            entry["result"] = "FAIL_MOUNT_PATH_UNRESOLVED"
            entry["error"] = "Mounted plugin did not return a usable virtual asset path"
            payload["failures"].append(
                {
                    "reason": "PROJECT_PLUGIN_MOUNT_PATH_UNRESOLVED",
                    "plugin": entry["plugin"],
                }
            )
            descriptors.append(entry)
            continue

        entry["mount_point"] = mount_point
        entry["result"] = "MOUNTED"
        content_roots[mount_point] = content_dir
        descriptors.append(entry)

    payload["plugin_mounts"] = descriptors
    return content_roots


def add_failure(payload, reason, **details):
    row = {"reason": reason}
    row.update(details)
    payload["failures"].append(row)


def compile_all_blueprints(payload):
    mount_content_roots = discover_project_plugin_mounts(payload)
    mount_content_roots["/Game"] = PROJECT_DIR / "Content"
    scope_roots = ["/Game"] + sorted(
        mount_point for mount_point in mount_content_roots if mount_point != "/Game"
    )
    payload["scope_roots"] = scope_roots
    payload["mount_content_roots"] = {
        mount: str(path.relative_to(PROJECT_DIR)).replace("\\", "/")
        for mount, path in sorted(mount_content_roots.items())
    }

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        registry.scan_paths_synchronous(scope_roots, True)
        registry.wait_for_completion()
    except Exception as exc:
        add_failure(payload, "ASSET_REGISTRY_SCAN_FAILED", error=repr(exc))
        return

    candidates = {}
    for root in scope_roots:
        try:
            asset_rows = registry.get_assets_by_path(
                root, recursive=True, include_only_on_disk_assets=True
            )
        except Exception as exc:
            add_failure(payload, "ASSET_REGISTRY_QUERY_FAILED", root=root, error=repr(exc))
            continue

        for asset_data in asset_rows:
            class_name = asset_class_name(asset_data)
            if not is_blueprint_asset_class(class_name):
                continue
            package = package_name(asset_data)
            if not package:
                add_failure(
                    payload,
                    "BLUEPRINT_ASSET_WITHOUT_PACKAGE_NAME",
                    root=root,
                    registry_class=class_name,
                )
                continue
            candidates.setdefault(
                package,
                {"package": package, "scope_root": root, "registry_class": class_name},
            )

    if not candidates:
        add_failure(payload, "NO_BLUEPRINT_ASSETS_DISCOVERED", scope_roots=scope_roots)
        return

    payload["discovered_blueprint_count"] = len(candidates)
    package_snapshots_before = {}
    for package, candidate in sorted(candidates.items()):
        before = package_file_snapshots(package, mount_content_roots)
        package_snapshots_before[package] = before
        if before is None or before.get(".uasset") is None:
            add_failure(
                payload,
                "BLUEPRINT_PACKAGE_FILE_UNRESOLVED",
                package=package,
                scope_root=candidate["scope_root"],
            )

    for package, candidate in sorted(candidates.items()):
        result = {
            "package": package,
            "scope_root": candidate["scope_root"],
            "registry_class": candidate["registry_class"],
            "loaded_class": "",
            "status": "",
            "result": "FAIL",
            "error": "",
        }
        try:
            asset = unreal.EditorAssetLibrary.load_asset(package)
        except Exception as exc:
            asset = None
            result["error"] = repr(exc)

        if asset is None:
            if not result["error"]:
                result["error"] = "EditorAssetLibrary.load_asset returned None"
            add_failure(
                payload,
                "BLUEPRINT_LOAD_FAILED",
                package=package,
                error=result["error"],
            )
            payload["blueprints"].append(result)
            continue

        result["loaded_class"] = safe_text(asset.get_class().get_name())
        if not isinstance(asset, unreal.Blueprint):
            result["error"] = "Loaded asset is not an unreal.Blueprint"
            add_failure(
                payload,
                "LOADED_ASSET_IS_NOT_BLUEPRINT",
                package=package,
                loaded_class=result["loaded_class"],
            )
            payload["blueprints"].append(result)
            continue

        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            status, normalized = normalized_blueprint_status(asset)
            result["status"] = status
            if "UPTODATE" not in normalized:
                result["error"] = "Blueprint did not reach UP_TO_DATE after compile"
                add_failure(
                    payload,
                    "BLUEPRINT_NOT_UP_TO_DATE",
                    package=package,
                    status=status,
                )
            else:
                result["result"] = "PASS_UP_TO_DATE"
        except Exception as exc:
            result["error"] = repr(exc)
            add_failure(
                payload,
                "BLUEPRINT_COMPILE_EXCEPTION",
                package=package,
                error=repr(exc),
            )
        payload["blueprints"].append(result)

    payload["compiled_blueprint_count"] = len(payload["blueprints"])
    for package, before in package_snapshots_before.items():
        after = package_file_snapshots(package, mount_content_roots)
        if before != after:
            payload["on_disk_package_changes"].append(
                {"package": package, "before": before, "after": after}
            )
            add_failure(payload, "BLUEPRINT_PACKAGE_FILE_CHANGED_DURING_VALIDATION", package=package)

    try:
        dirty_packages = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        payload["dirty_content_packages_after_compile"] = sorted(
            object_path(package) for package in dirty_packages
        )
    except Exception as exc:
        payload["dirty_content_packages_after_compile_error"] = repr(exc)


def write_report(payload):
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def make_payload():
    return {
        "schema_version": 1,
        "status": "IN_PROGRESS",
        "started_utc": utc_now(),
        "finished_utc": None,
        "project": str(PROJECT_DIR),
        "report": str(REPORT),
        "scope_roots": [],
        "mount_content_roots": {},
        "plugin_mounts": [],
        "discovered_blueprint_count": 0,
        "compiled_blueprint_count": 0,
        "blueprints": [],
        "failures": [],
        "on_disk_package_changes": [],
        "dirty_content_packages_after_compile": [],
        "saved_assets": False,
        "save_api_called": False,
    }


PAYLOAD = make_payload()
if REPORT_PATH_ERROR:
    add_failure(PAYLOAD, "INVALID_REPORT_PATH", error=REPORT_PATH_ERROR)

try:
    if not PAYLOAD["failures"]:
        compile_all_blueprints(PAYLOAD)
except BaseException as exc:
    add_failure(
        PAYLOAD,
        "UNHANDLED_GATE_EXCEPTION",
        error=repr(exc),
        traceback=traceback.format_exc(),
    )
finally:
    PAYLOAD["finished_utc"] = utc_now()
    PAYLOAD["status"] = (
        "UE58_ALL_PROJECT_BLUEPRINT_COMPILE_PASS"
        if not PAYLOAD["failures"]
        and PAYLOAD["discovered_blueprint_count"] > 0
        and PAYLOAD["compiled_blueprint_count"] == PAYLOAD["discovered_blueprint_count"]
        and not PAYLOAD["on_disk_package_changes"]
        else "UE58_ALL_PROJECT_BLUEPRINT_COMPILE_FAIL"
    )
    try:
        write_report(PAYLOAD)
        unreal.log(
            "[AllProjectBlueprintCompile58] {} report={}".format(
                PAYLOAD["status"], REPORT
            )
        )
    except Exception as exc:
        unreal.log_error(
            "[AllProjectBlueprintCompile58] unable to write report: " + repr(exc)
        )
    unreal.SystemLibrary.quit_editor()
