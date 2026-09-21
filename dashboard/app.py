#!/usr/bin/env python3
"""Local read-only dashboard for Wakaama Bootstrap and LwM2M Server logs."""

import json
import os
import re
import socket
import subprocess
import tempfile
import hashlib
import cgi
import signal
import threading
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

BASE_DIR = Path(__file__).resolve().parent
PROJECT_DIR = BASE_DIR.parent
STATIC_DIR = BASE_DIR / "static"
DEFAULT_LOG_DIR = BASE_DIR / "logs"
DEFAULT_BOOTSTRAP_INI = PROJECT_DIR / "run" / "bootstrap_server" / "01_bs_plain.ini"
BOOTSTRAP_INI = Path(os.environ.get("WAKAAMA_BOOTSTRAP_INI", DEFAULT_BOOTSTRAP_INI))
BACKUP_DIR = Path(os.environ.get("WAKAAMA_BACKUP_DIR", BASE_DIR / "backups"))
RESTART_COMMAND = os.environ.get("WAKAAMA_BOOTSTRAP_RESTART_COMMAND", "")
BOOTSTRAP_SERVICE = os.environ.get("WAKAAMA_BOOTSTRAP_SERVICE", "")
SERVER_SERVICE = os.environ.get("WAKAAMA_SERVER_SERVICE", "")
BOOTSTRAP_LOG = Path(os.environ.get("WAKAAMA_BOOTSTRAP_LOG", DEFAULT_LOG_DIR / "bootstrap.log"))
SERVER_LOG = Path(os.environ.get("WAKAAMA_SERVER_LOG", DEFAULT_LOG_DIR / "server.log"))
CONTROL_SOCKET = os.environ.get("WAKAAMA_CONTROL_SOCKET", "/tmp/lwm2mserver-control.sock")
DFOTA_FIRMWARE_DIR = Path(os.environ.get("WAKAAMA_DFOTA_FIRMWARE_DIR", PROJECT_DIR / "run" / "server" / "dfota_fw"))
RUNTIME_MODE = os.environ.get("WAKAAMA_RUNTIME_MODE", "external").lower()
BOOTSTRAP_PORT = int(os.environ.get("WAKAAMA_BOOTSTRAP_PORT", "22101"))
LWM2M_PORT = int(os.environ.get("WAKAAMA_LWM2M_PORT", "22102"))
BOOTSTRAP_BINARY = os.environ.get("WAKAAMA_BOOTSTRAP_BINARY", str(PROJECT_DIR / "build" / "bootstrap_server" / "bootstrap_server"))
LWM2M_SERVER_BINARY = os.environ.get("WAKAAMA_LWM2M_SERVER_BINARY", str(PROJECT_DIR / "build" / "server" / "lwm2mserver"))
DFOTA_HOST = os.environ.get("WAKAAMA_DFOTA_HOST", "")
DFOTA_URI_PREFIX = os.environ.get("WAKAAMA_DFOTA_URI_PREFIX", "/dfota_fw")
COMMERCIAL_SERVER_MODE = os.environ.get("WAKAAMA_COMMERCIAL_SERVER_MODE", "1") == "1"
MAX_FIRMWARE_BYTES = 64 * 1024 * 1024
ALLOWED_FIRMWARE_SUFFIXES = (".bin", ".img", ".hex")
MAX_LOG_BYTES = 48 * 1024
LISTEN_HOST = os.environ.get("WAKAAMA_DASHBOARD_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("WAKAAMA_DASHBOARD_PORT", "8080"))
MANAGED_PROCESSES = {}
MANAGED_PROCESS_LOCK = threading.Lock()


def read_log(path):
    try:
        with path.open("rb") as log_file:
            log_file.seek(0, os.SEEK_END)
            file_size = log_file.tell()
            log_file.seek(max(0, file_size - MAX_LOG_BYTES))
            contents = log_file.read().decode("utf-8", errors="replace")
    except FileNotFoundError:
        return {"path": str(path), "available": False, "contents": "Log file has not been created yet."}
    except OSError as error:
        return {"path": str(path), "available": False, "contents": "Unable to read log: {}".format(error)}

    if file_size > MAX_LOG_BYTES:
        first_newline = contents.find("\n")
        contents = contents[first_newline + 1:] if first_newline >= 0 else contents
    return {"path": str(path), "available": True, "contents": contents}


def capture_tmux_log(markers):
    if isinstance(markers, str):
        markers = (markers,)
    try:
        panes = subprocess.run(
            ["tmux", "list-panes", "-a", "-F", "#{session_name}:#{window_index}.#{pane_index}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError:
        return None

    for pane in panes.stdout.splitlines():
        output = subprocess.run(
            ["tmux", "capture-pane", "-p", "-t", pane, "-S", "-500"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if any(marker in output.stdout for marker in markers):
            return {"path": "tmux pane {}".format(pane), "available": True, "contents": output.stdout}
    return None


def read_service_log(path, tmux_marker, systemd_service=""):
    log = read_log(path)
    if log["available"] and log["contents"].strip():
        return log
    if systemd_service and systemd_service_active(systemd_service):
        journal_log = read_journal_log(systemd_service)
        if journal_log:
            return journal_log
    tmux_log = capture_tmux_log(tmux_marker)
    return tmux_log if tmux_log else log


def parse_registered_clients(contents):
    clients = {}
    models = {}
    client = None
    for line in contents.splitlines():
        model = re.match(r'^\s*Client #(\d+) model: "(.*)"\s*$', line)
        if model:
            models[int(model.group(1))] = model.group(2)
            continue
        header = re.match(r"^\s*Client #(\d+):\s*$", line)
        if header:
            if client and client["name"]:
                clients[client["id"]] = client
            client = {"id": int(header.group(1)), "name": "", "version": "", "binding": "", "lifetime": "", "model": "", "objects": ""}
            continue
        if client is None:
            continue
        field = re.match(r"^\s*(name|version|binding|lifetime|objects):\s*(.+)$", line)
        if field:
            key, value = field.groups()
            value = value.strip().strip('"')
            client[key] = value[:-1] if value.endswith(",") else value
        elif client["objects"] and re.match(r"^\s*/[0-9]", line):
            client["objects"] = "{}, {}".format(client["objects"], line.strip().rstrip(","))
    if client and client["name"]:
        clients[client["id"]] = client
    for client_id, model in models.items():
        if client_id in clients:
            clients[client_id]["model"] = model
    return sorted(clients.values(), key=lambda client: client["id"])


def binding_label(value):
    try:
        binding = int(value)
    except (TypeError, ValueError):
        return str(value)

    labels = []
    for flag, label in ((0x02, "UDP"), (0x04, "TCP"), (0x08, "SMS"),
                        (0x10, "Non-IP"), (0x20, "Queue")):
        if binding & flag:
            labels.append(label)
    if labels:
        return ", ".join(labels)
    return "Not specified" if binding & 0x01 else str(value)


def request_control_list():
    client_socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    client_socket.settimeout(1)
    temporary_path = None
    try:
        file_descriptor, temporary_path = tempfile.mkstemp(prefix="wakaama-dashboard-", dir="/tmp")
        os.close(file_descriptor)
        os.unlink(temporary_path)
        client_socket.bind(temporary_path)
        client_socket.sendto(b"LIST", CONTROL_SOCKET)
        response = client_socket.recv(8192).decode("utf-8", errors="replace")
    except OSError as error:
        raise ValueError("Unable to request live client list: {}".format(error))
    finally:
        client_socket.close()
        if temporary_path:
            try:
                os.unlink(temporary_path)
            except FileNotFoundError:
                pass

    clients = []
    for line in response.splitlines():
        fields = line.split("\t")
        if len(fields) not in (5, 6) or not fields[0].isdigit():
            continue
        clients.append({"id": int(fields[0]), "name": fields[1], "version": fields[2],
                        "binding": binding_label(fields[3]), "lifetime": "{} sec".format(fields[4]),
                        "model": fields[5] if len(fields) == 6 else "", "objects": ""})
    return sorted(clients, key=lambda client: client["id"])


def registered_clients():
    try:
        return {"source": "local control socket", "available": True, "clients": request_control_list()}
    except ValueError as error:
        fallback_error = str(error)
    log = read_service_log(SERVER_LOG, ("lwm2mserver", "Client #", "Notify from client", "[lwm2m_handle_packet:"), SERVER_SERVICE)
    return {"source": "{} (fallback: {})".format(log["path"], fallback_error), "available": log["available"], "clients": parse_registered_clients(log["contents"])}


def queue_write(endpoint_name, uri, value):
    if not isinstance(endpoint_name, str) or not isinstance(uri, str) or not isinstance(value, str):
        raise ValueError("endpoint, URI, and value must be strings")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", endpoint_name):
        raise ValueError("Invalid endpoint name")
    if not re.fullmatch(r"/(?:[0-9]{1,5})(?:/[0-9]{1,5}){0,2}", uri):
        raise ValueError("URI must be a LwM2M path such as /10250/0/1")
    if not value or "\n" in value or "\r" in value:
        raise ValueError("Value must be a non-empty single line")

    client = next((item for item in registered_clients()["clients"] if item["name"] == endpoint_name), None)
    if client is None:
        raise ValueError("Endpoint is not currently registered: {}".format(endpoint_name))
    request = "WRITE\t{}\t{}\t{}".format(client["id"], uri, value).encode("utf-8")
    if len(request) >= 1024:
        raise ValueError("Write request is too long")

    control_socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        control_socket.sendto(request, CONTROL_SOCKET)
    except OSError as error:
        raise ValueError("Unable to queue write: {}".format(error))
    finally:
        control_socket.close()
    return {"endpoint": endpoint_name, "client_id": client["id"], "uri": uri, "queued_at": datetime.now(timezone.utc).isoformat()}


def firmware_catalog():
    firmware_dir = DFOTA_FIRMWARE_DIR.resolve()
    if not firmware_dir.is_dir():
        raise ValueError("Firmware directory is unavailable: {}".format(firmware_dir))
    artifacts = []
    for path in sorted(firmware_dir.iterdir()):
        resolved = path.resolve()
        if not path.is_file() or resolved.parent != firmware_dir:
            continue
        artifacts.append(firmware_metadata(resolved))
    return {"directory": str(firmware_dir), "artifacts": artifacts}


def firmware_metadata(path):
    digest = hashlib.sha256()
    with path.open("rb") as firmware_file:
        for chunk in iter(lambda: firmware_file.read(64 * 1024), b""):
            digest.update(chunk)
    return {"name": path.name, "size": path.stat().st_size, "sha256": digest.hexdigest()}


def upload_firmware(file_item):
    firmware_dir = DFOTA_FIRMWARE_DIR.resolve()
    if not firmware_dir.is_dir():
        raise ValueError("Firmware directory is unavailable: {}".format(firmware_dir))
    filename = file_item.filename or ""
    if Path(filename).name != filename or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", filename):
        raise ValueError("Invalid firmware file name")
    if Path(filename).suffix.lower() not in ALLOWED_FIRMWARE_SUFFIXES:
        raise ValueError("Firmware file must use one of: {}".format(", ".join(ALLOWED_FIRMWARE_SUFFIXES)))
    destination = firmware_dir / filename
    if destination.exists():
        raise ValueError("Firmware file already exists: {}".format(filename))

    file_descriptor, temporary_path = tempfile.mkstemp(prefix=".upload-", suffix=".tmp", dir=str(firmware_dir))
    size = 0
    try:
        with os.fdopen(file_descriptor, "wb") as temporary_file:
            while True:
                chunk = file_item.file.read(64 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_FIRMWARE_BYTES:
                    raise ValueError("Firmware file exceeds the 64 MiB limit")
                temporary_file.write(chunk)
            if size == 0:
                raise ValueError("Firmware file is empty")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, destination)
    except Exception:
        try:
            os.unlink(temporary_path)
        except FileNotFoundError:
            pass
        raise
    return firmware_metadata(destination)


def queue_dfota(endpoint_name, filename):
    if not isinstance(endpoint_name, str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", endpoint_name):
        raise ValueError("Invalid endpoint name")
    if not isinstance(filename, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", filename) or ".." in filename:
        raise ValueError("Invalid firmware file name")
    artifact = next((item for item in firmware_catalog()["artifacts"] if item["name"] == filename), None)
    if artifact is None:
        raise ValueError("Firmware artifact is not approved: {}".format(filename))
    client = next((item for item in registered_clients()["clients"] if item["name"] == endpoint_name), None)
    if client is None:
        raise ValueError("Endpoint is not currently registered: {}".format(endpoint_name))
    request = "DFOTA\t{}\t{}".format(client["id"], filename).encode("utf-8")
    control_socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        control_socket.sendto(request, CONTROL_SOCKET)
    except OSError as error:
        raise ValueError("Unable to queue DFOTA: {}".format(error))
    finally:
        control_socket.close()
    return {"endpoint": endpoint_name, "client_id": client["id"], "artifact": artifact, "queued_at": datetime.now(timezone.utc).isoformat()}


def systemd_service_active(service_name):
    result = subprocess.run(["systemctl", "--user", "is-active", "--quiet", service_name],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return result.returncode == 0


def systemd_service_property(service_name, property_name):
    result = subprocess.run(
        ["systemctl", "--user", "show", "--property={}".format(property_name), "--value", service_name],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        encoding="utf-8", errors="replace", check=False)
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    return value or None


def service_activation_time(service_name):
    return systemd_service_property(service_name, "ActiveEnterTimestamp")


def journal_message(entry):
    message = entry.get("MESSAGE")
    if isinstance(message, str):
        return message
    if isinstance(message, list):
        try:
            return bytes(message).decode("utf-8", errors="replace")
        except (TypeError, ValueError):
            return None
    return None


def read_journal_log(service_name):
    command = ["journalctl", "--user", "-u", service_name]
    activation_time = service_activation_time(service_name)
    if activation_time:
        command.extend(["--since", activation_time])
    invocation_id = systemd_service_property(service_name, "InvocationID")
    if invocation_id:
        command.append("_SYSTEMD_INVOCATION_ID={}".format(invocation_id))
    command.extend(["--reverse", "-n", "2000", "--no-pager", "-o", "json"])
    result = subprocess.run(command,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                            encoding="utf-8", errors="replace", check=False)
    if result.returncode != 0:
        return None
    messages = []
    for line in result.stdout.splitlines():
        try:
            message = journal_message(json.loads(line))
        except (json.JSONDecodeError, TypeError):
            continue
        if message is not None:
            messages.append(message)
    contents = "\n".join(reversed(messages))
    if len(contents.encode("utf-8")) > MAX_LOG_BYTES:
        contents = contents.encode("utf-8")[-MAX_LOG_BYTES:].decode("utf-8", errors="replace")
        first_newline = contents.find("\n")
        contents = contents[first_newline + 1:] if first_newline >= 0 else contents
    return {"path": "systemd journal {}".format(service_name), "available": True, "contents": contents}


def parse_bootstrap_ini(contents):
    sections = []
    current = None
    for line_number, original_line in enumerate(contents.splitlines(), 1):
        line = original_line.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section_name = line[1:-1].strip().lower()
            if section_name not in ("server", "endpoint"):
                raise ValueError("Line {}: unsupported section '{}'".format(line_number, line))
            current = {"type": section_name, "values": {}, "line": line_number}
            sections.append(current)
            continue
        if current is None or "=" not in line:
            raise ValueError("Line {}: expected a section or key=value pair".format(line_number))
        key, value = (part.strip() for part in line.split("=", 1))
        if not key:
            raise ValueError("Line {}: key cannot be empty".format(line_number))
        normalized_key = key.lower()
        if normalized_key in current["values"]:
            raise ValueError("Line {}: duplicate key '{}'".format(line_number, key))
        current["values"][normalized_key] = value
    return sections


def validate_bootstrap_ini(contents):
    sections = parse_bootstrap_ini(contents)
    server_ids = set()
    endpoint_names = set()
    endpoints = []
    for section in sections:
        values = section["values"]
        if section["type"] == "server":
            for key in ("id", "uri", "security"):
                if not values.get(key):
                    raise ValueError("Line {}: Server requires '{}'".format(section["line"], key))
            if values["id"] in server_ids:
                raise ValueError("Line {}: duplicate Server id '{}'".format(section["line"], values["id"]))
            server_ids.add(values["id"])
        elif values.get("name"):
            endpoint_name = values["name"]
            if endpoint_name in endpoint_names:
                raise ValueError("Line {}: duplicate Endpoint Name '{}'".format(section["line"], endpoint_name))
            endpoint_names.add(endpoint_name)
            endpoints.append(endpoint_name)
    for section in sections:
        if section["type"] == "endpoint" and section["values"].get("server"):
            server_id = section["values"]["server"]
            if server_id not in server_ids:
                raise ValueError("Line {}: Endpoint references unknown Server id '{}'".format(section["line"], server_id))
    return {"server_ids": sorted(server_ids), "endpoints": sorted(endpoints)}


def read_bootstrap_ini():
    contents = BOOTSTRAP_INI.read_text(encoding="utf-8")
    metadata = validate_bootstrap_ini(contents)
    return {
        "path": str(BOOTSTRAP_INI),
        "contents": contents,
        "restart_configured": bool(RESTART_COMMAND or BOOTSTRAP_SERVICE),
        "restart_mode": "systemd" if BOOTSTRAP_SERVICE else "command" if RESTART_COMMAND else "disabled",
        **metadata
    }


def write_bootstrap_ini(contents):
    metadata = validate_bootstrap_ini(contents)
    if not BOOTSTRAP_INI.exists():
        raise ValueError("Bootstrap INI does not exist: {}".format(BOOTSTRAP_INI))
    BACKUP_DIR.mkdir(exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_path = BACKUP_DIR / "bootstrap-{}.ini".format(timestamp)
    original = BOOTSTRAP_INI.read_text(encoding="utf-8")
    backup_path.write_text(original, encoding="utf-8")
    file_descriptor, temporary_path = tempfile.mkstemp(prefix=".bootstrap-", suffix=".ini", dir=str(BOOTSTRAP_INI.parent))
    try:
        with os.fdopen(file_descriptor, "w", encoding="utf-8") as temporary_file:
            temporary_file.write(contents)
            if not contents.endswith("\n"):
                temporary_file.write("\n")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, BOOTSTRAP_INI)
    except Exception:
        os.unlink(temporary_path)
        raise
    return {"backup": str(backup_path), **metadata}


def restart_bootstrap_server():
    if RUNTIME_MODE == "managed":
        return control_managed_service("bootstrap", "restart")
    if BOOTSTRAP_SERVICE:
        if not re.fullmatch(r"[A-Za-z0-9_.@-]+\.service", BOOTSTRAP_SERVICE):
            raise ValueError("WAKAAMA_BOOTSTRAP_SERVICE must be a systemd .service unit name")
        result = subprocess.run(["systemctl", "--user", "restart", BOOTSTRAP_SERVICE], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=20, check=False)
        if result.returncode != 0:
            raise ValueError("Systemd restart failed (exit {}): {}".format(result.returncode, result.stdout[-2000:]))
        return {"service": BOOTSTRAP_SERVICE, "output": result.stdout[-2000:]}
    if not RESTART_COMMAND:
        raise ValueError("Restart is not configured. Set WAKAAMA_BOOTSTRAP_RESTART_COMMAND before starting the dashboard.")
    result = subprocess.run(RESTART_COMMAND, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=20, check=False)
    if result.returncode != 0:
        raise ValueError("Restart command failed (exit {}): {}".format(result.returncode, result.stdout[-2000:]))
    return {"command": RESTART_COMMAND, "output": result.stdout[-2000:]}


def managed_service(target):
    services = {"bootstrap": BOOTSTRAP_SERVICE, "server": SERVER_SERVICE}
    service_name = services.get(target)
    if not service_name:
        raise ValueError("{} service control is not configured".format(target.capitalize()))
    if not re.fullmatch(r"[A-Za-z0-9_.@-]+\.service", service_name):
        raise ValueError("Invalid configured {} service name".format(target))
    return service_name


def control_service(target, action):
    if RUNTIME_MODE == "managed":
        return control_managed_service(target, action)
    if action not in ("start", "stop", "restart"):
        raise ValueError("Unsupported service action")
    service_name = managed_service(target)
    result = subprocess.run(["systemctl", "--user", action, service_name], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=20, check=False)
    if result.returncode != 0:
        raise ValueError("{} failed (exit {}): {}".format(action.capitalize(), result.returncode, result.stdout[-2000:]))
    return {"target": target, "action": action, "service": service_name, "active": systemd_service_active(service_name)}


def managed_command(target):
    if target == "bootstrap":
        return [BOOTSTRAP_BINARY, "-4", "-l", str(BOOTSTRAP_PORT), "-f", str(BOOTSTRAP_INI)]
    if target == "server":
        command = [LWM2M_SERVER_BINARY, "-4"]
        if COMMERCIAL_SERVER_MODE:
            command.append("-C")
        command.extend(["-l", str(LWM2M_PORT), "-p", CONTROL_SOCKET,
                        "-F", str(DFOTA_FIRMWARE_DIR), "-u", DFOTA_URI_PREFIX])
        if DFOTA_HOST:
            command.extend(["-H", DFOTA_HOST])
        return command
    raise ValueError("Unsupported managed service")


def managed_log_path(target):
    return BOOTSTRAP_LOG if target == "bootstrap" else SERVER_LOG


def start_managed_service(target):
    command = managed_command(target)
    executable = Path(command[0])
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError("Managed {} executable is unavailable: {}".format(target, executable))

    with MANAGED_PROCESS_LOCK:
        process = MANAGED_PROCESSES.get(target)
        if process and process.poll() is None:
            return {"target": target, "action": "start", "active": True, "already_running": True}
        if process_status(executable.name)["running"]:
            raise ValueError("{} is already running outside dashboard management".format(target.capitalize()))

        log_path = managed_log_path(target)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        if target == "server":
            Path(CONTROL_SOCKET).parent.mkdir(parents=True, exist_ok=True)
            DFOTA_FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
        with log_path.open("ab", buffering=0) as log_file:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log_file,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        MANAGED_PROCESSES[target] = process
    return {"target": target, "action": "start", "active": True, "pid": process.pid}


def stop_managed_service(target):
    with MANAGED_PROCESS_LOCK:
        process = MANAGED_PROCESSES.get(target)
        if process is None or process.poll() is not None:
            MANAGED_PROCESSES.pop(target, None)
            return {"target": target, "action": "stop", "active": False, "already_stopped": True}
        process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    with MANAGED_PROCESS_LOCK:
        MANAGED_PROCESSES.pop(target, None)
    return {"target": target, "action": "stop", "active": False}


def control_managed_service(target, action):
    if target not in ("bootstrap", "server") or action not in ("start", "stop", "restart"):
        raise ValueError("Unsupported managed service action")
    if action == "restart":
        stop_managed_service(target)
        result = start_managed_service(target)
    elif action == "start":
        result = start_managed_service(target)
    else:
        result = stop_managed_service(target)
    result["manager"] = "dashboard"
    return result


def stop_all_managed_services():
    if RUNTIME_MODE != "managed":
        return
    for target in ("server", "bootstrap"):
        try:
            stop_managed_service(target)
        except OSError:
            pass


def handle_shutdown_signal(signum, frame):
    raise KeyboardInterrupt


def process_status(process_name):
    result = subprocess.run(["pgrep", "-af", process_name], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, check=False)
    lines = [line for line in result.stdout.splitlines() if "pgrep -af" not in line]
    return {"running": bool(lines), "processes": lines}


def udp_port_open(port):
    port_hex = "{:04X}".format(port)
    for proc_file in ("/proc/net/udp", "/proc/net/udp6"):
        try:
            with open(proc_file, encoding="utf-8") as socket_file:
                for line in socket_file.readlines()[1:]:
                    fields = line.split()
                    if len(fields) >= 4 and fields[1].split(":")[1] == port_hex and fields[3] == "07":
                        return True
        except OSError:
            continue
    return False


def dashboard_status():
    bootstrap = {"port": BOOTSTRAP_PORT, "listener": udp_port_open(BOOTSTRAP_PORT), **process_status("bootstrap_server")}
    if BOOTSTRAP_SERVICE:
        bootstrap["service"] = BOOTSTRAP_SERVICE
        bootstrap["service_active"] = systemd_service_active(BOOTSTRAP_SERVICE)
    if RUNTIME_MODE == "managed":
        bootstrap["manager"] = "dashboard"
    server = {"port": LWM2M_PORT, "listener": udp_port_open(LWM2M_PORT), **process_status("lwm2mserver")}
    if SERVER_SERVICE:
        server["service"] = SERVER_SERVICE
        server["service_active"] = systemd_service_active(SERVER_SERVICE)
    if RUNTIME_MODE == "managed":
        server["manager"] = "dashboard"
    return {
        "bootstrap": bootstrap,
        "server": server,
    }


class DashboardHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC_DIR), **kwargs)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_json(dashboard_status())
            return
        if path == "/api/logs":
            self.send_json({
                "bootstrap": read_service_log(BOOTSTRAP_LOG, "bootstrap_server", BOOTSTRAP_SERVICE),
                "server": read_service_log(SERVER_LOG, ("lwm2mserver", "Client #", "Notify from client", "[lwm2m_handle_packet:"), SERVER_SERVICE),
            })
            return
        if path == "/api/clients":
            self.send_json(registered_clients())
            return
        if path == "/api/firmware":
            try:
                self.send_json(firmware_catalog())
            except ValueError as error:
                self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
            return
        if path == "/api/bootstrap/ini":
            try:
                self.send_json(read_bootstrap_ini())
            except (OSError, ValueError) as error:
                self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
            return
        if path == "/":
            self.path = "/index.html"
        super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/firmware/upload":
            try:
                self.send_json(upload_firmware(self.read_firmware_upload()), HTTPStatus.CREATED)
            except (OSError, ValueError) as error:
                self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
            return
        if path.startswith("/api/services/"):
            parts = path.split("/")
            if len(parts) != 5:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            try:
                self.send_json(control_service(parts[3], parts[4]), HTTPStatus.OK)
            except ValueError as error:
                self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
            return
        if path not in ("/api/bootstrap/ini", "/api/bootstrap/restart", "/api/write", "/api/dfota"):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            if path == "/api/bootstrap/ini":
                payload = self.read_json_body()
                contents = payload.get("contents")
                if not isinstance(contents, str):
                    raise ValueError("'contents' must be a string")
                self.send_json(write_bootstrap_ini(contents), HTTPStatus.OK)
            elif path == "/api/bootstrap/restart":
                self.send_json(restart_bootstrap_server(), HTTPStatus.OK)
            elif path == "/api/write":
                payload = self.read_json_body()
                self.send_json(queue_write(payload.get("endpoint"), payload.get("uri"), payload.get("value")), HTTPStatus.OK)
            else:
                payload = self.read_json_body()
                self.send_json(queue_dfota(payload.get("endpoint"), payload.get("filename")), HTTPStatus.OK)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)

    def read_json_body(self):
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0 or content_length > 512 * 1024:
            raise ValueError("Request body must be between 1 and 524288 bytes")
        return json.loads(self.rfile.read(content_length).decode("utf-8"))

    def read_firmware_upload(self):
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0 or content_length > MAX_FIRMWARE_BYTES + 1024 * 1024:
            raise ValueError("Upload must be between 1 byte and 64 MiB")
        content_type = self.headers.get("Content-Type", "")
        if not content_type.startswith("multipart/form-data"):
            raise ValueError("Firmware upload must use multipart/form-data")
        form = cgi.FieldStorage(fp=self.rfile, headers=self.headers,
                                environ={"REQUEST_METHOD": "POST", "CONTENT_TYPE": content_type})
        if "firmware" not in form or not getattr(form["firmware"], "file", None):
            raise ValueError("Firmware upload field is missing")
        return form["firmware"]

    def send_json(self, payload, status=HTTPStatus.OK):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_string, *args):
        return


def main():
    DEFAULT_LOG_DIR.mkdir(exist_ok=True)
    server = ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), DashboardHandler)
    signal.signal(signal.SIGTERM, handle_shutdown_signal)
    print("Wakaama dashboard: http://{}:{}".format(LISTEN_HOST, LISTEN_PORT))
    print("Bootstrap log: {}".format(BOOTSTRAP_LOG))
    print("Server log: {}".format(SERVER_LOG))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard stopped.")
    finally:
        server.server_close()
        stop_all_managed_services()


if __name__ == "__main__":
    main()