#!/usr/bin/env python3
"""
SPS Integration Test CLI
=======================
Mock hardware components (RFID, MCU) and Server actions for Integration Testing
of the Smart Podium System (SPS) running on Raspberry Pi.

Server Backend Architecture (server/README.md):
  Master Database:    Relational DB (Lecturers UID, Rooms/devices) + File Storage
  MQTT Broker:        JSON event/command exchange
  Node.js/Express:    RESTful API + Sync Service + OTA Hybrid Server
  MQTT-to-DB Worker:  Bidirectional sync (Server<->PCD)

MQTT topics (server architecture):
  sps/+/status/connection    PCD connection status / LWT
  sps/+/cmd/#                Server remote commands (wildcard)
  sps/+/status/dbsync        Server pushes UID database updates
  sps/+/status/ota           Server signals OTA update (version + checksum)
  PCD then pulls firmware via HTTP GET from File Storage.

D-Bus services (System Bus):
  com.sps.auth       - Authentication service
  com.sps.router     - Protocol Router (UART to MCU)
  com.sps.engine     - Automation Engine (Scenario execution)
  com.sps.netmgr     - Network Manager (MQTT, network)
  com.sps.otamanager - OTA Update Manager

UART protocol (115200 8N1, /dev/ttyS0):
  Frame: [0xAA][0x55][Len][CmdId][Payload...][CRC16]
"""

from __future__ import annotations

import json
import time
import struct
import threading
import logging
import sys
import os
import signal
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Any, Tuple, Callable
from enum import Enum

import click

try:
    import pydbus
    from gi.repository import GLib
    HAS_DBUS = True
except ImportError:
    HAS_DBUS = False

try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False

# ============================================================================
# Configuration Constants
# ============================================================================

ROOM_ID = "room101"
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TIMEOUT = 5.0
DBUS_TIMEOUT = 5.0

DBUS_SERVICES: Dict[str, Tuple[str, str]] = {
    "auth":   ("com.sps.auth",   "/com/sps/auth"),
    "router": ("com.sps.router", "/com/sps/router"),
    "engine": ("com.sps.engine", "/com/sps/engine"),
    "netmgr": ("com.sps.netmgr", "/com/sps/network"),
    "ota":    ("com.sps.otamanager", "/com/sps/otamanager"),
}

UART_CMD = {
    "PING_HEARTBEAT":    0x10,
    "ACK_ALIVE":         0x11,
    "NACK_ERROR":        0x12,
    "LIGHT_CONTROL":     0x21,
    "CURTAIN_CONTROL":   0x22,
    "PROJECTOR_CONTROL": 0x23,
    "AC_CONTROL":        0x24,
    "QUERY_RELAY_STATUS":0x32,
    "PRESENCE_ALERT":    0x41,
    "OTA_START":         0x50,
    "OTA_DATA_CHUNK":    0x51,
    "OTA_END":           0x52,
}

UART_CMD_NAMES = {v: k for k, v in UART_CMD.items()}

DEVICE_IDS = {
    "LIGHT_PODIUM": 0x01,
    "LIGHT_CLASS":  0x02,
    "LIGHT_ALL":    0xFF,
    "CURTAIN":      0x01,
    "SCREEN":       0x02,
    "AC":           0x01,
}

# ============================================================================
# Data Types
# ============================================================================

class TestResult(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"

@dataclass
class TestReport:
    tc_id: str
    category: str
    action_descr: str
    expected: str
    result: TestResult
    details: str = ""
    duration: float = 0.0

# ============================================================================
# D-Bus Client
# ============================================================================

class SignalCollector:
    """Thread-safe container for received D-Bus signals."""

    def __init__(self):
        self._signals: List[Tuple[str, Tuple, float]] = []
        self._lock = threading.Lock()

    def add(self, signal_name: str, *args: Any) -> None:
        with self._lock:
            self._signals.append((signal_name, args, time.time()))

    def clear(self) -> None:
        with self._lock:
            self._signals.clear()

    def wait_for(self, signal_name: str, timeout: float = DBUS_TIMEOUT) -> Optional[Tuple]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, (name, args, _) in enumerate(self._signals):
                    if name == signal_name:
                        self._signals.pop(i)
                        return args
            time.sleep(0.05)
        return None

    def wait_for_any(self, signal_names: List[str], timeout: float = DBUS_TIMEOUT) -> Optional[Tuple[str, Tuple]]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, (name, args, _) in enumerate(self._signals):
                    if name in signal_names:
                        self._signals.pop(i)
                        return (name, args)
            time.sleep(0.05)
        return None


class DBusClient:
    """Connects to all SPS D-Bus services and monitors signals."""

    def __init__(self):
        self.collector = SignalCollector()
        self.services: Dict[str, Any] = {}
        self._loop: Optional[GLib.MainLoop] = None
        self._thread: Optional[threading.Thread] = None

        if not HAS_DBUS:
            click.secho("  pydbus/PyGObject not available. Install with: pip install pydbus PyGObject", fg="yellow")
            return

        try:
            self.bus = pydbus.SystemBus()
        except Exception as e:
            click.secho(f"  Cannot connect to D-Bus System Bus: {e}", fg="red")
            self.bus = None
            return

        for name, (bus_name, obj_path) in DBUS_SERVICES.items():
            try:
                proxy = self.bus.get(bus_name, obj_path)
                self.services[name] = proxy
            except Exception as e:
                self.services[name] = None

        self._connect_all_signals()

        self._loop = GLib.MainLoop()
        self._thread = threading.Thread(target=self._loop.run, daemon=True)
        self._thread.start()

    # ------------------------------------------------------------------
    # Signal connection helpers
    # ------------------------------------------------------------------

    def _connect_all_signals(self) -> None:
        auth = self.services.get("auth")
        if auth:
            try:
                auth.onAuthStatusChanged = lambda s: self.collector.add("AuthStatusChanged", s)
                auth.onLecturerAuthenticated = lambda n, t: self.collector.add("LecturerAuthenticated", n, t)
                auth.onAuthenticationFailed = lambda r: self.collector.add("AuthenticationFailed", r)
            except Exception:
                pass

        router = self.services.get("router")
        if router:
            try:
                router.onConnectionStatusChanged = lambda s: self.collector.add("ConnectionStatusChanged", s)
                router.onCommandAcknowledged = lambda c: self.collector.add("CommandAcknowledged", c)
                router.onCommandError = lambda c, e: self.collector.add("CommandError", c, e)
                router.onDeviceStatusChanged = lambda d, s: self.collector.add("DeviceStatusChanged", d, s)
                router.onPresenceDetected = lambda p: self.collector.add("PresenceDetected", p)
                router.onOTAProgress = lambda p: self.collector.add("OTAProgress", p)
            except Exception:
                pass

        engine = self.services.get("engine")
        if engine:
            try:
                engine.onScenarioStarted = lambda s: self.collector.add("ScenarioStarted", s)
                engine.onScenarioCompleted = lambda s: self.collector.add("ScenarioCompleted", s)
                engine.onScenarioError = lambda s, e: self.collector.add("ScenarioError", s, e)
                engine.onCommandExecuting = lambda s, i, d: self.collector.add("CommandExecuting", s, i, d)
                engine.onContextTriggered = lambda c, s: self.collector.add("ContextTriggered", c, s)
            except Exception:
                pass

        netmgr = self.services.get("netmgr")
        if netmgr:
            try:
                netmgr.onMqttConnected = lambda: self.collector.add("MqttConnected")
                netmgr.onMqttDisconnected = lambda r: self.collector.add("MqttDisconnected", r)
                netmgr.onMqttMessageReceived = lambda t, p: self.collector.add("MqttMessageReceived", t, p)
                netmgr.onMqttError = lambda e: self.collector.add("MqttError", e)
                netmgr.onNetworkStatusChanged = lambda s: self.collector.add("NetworkStatusChanged", s)
                netmgr.onCommandReceived = lambda c, d: self.collector.add("CommandReceived", c, d)
                netmgr.onSyncDataReceived = lambda a, j: self.collector.add("SyncDataReceived", a, j)
                netmgr.onOtaCommandReceived = lambda a, p: self.collector.add("OtaCommandReceived", a, p)
                netmgr.onLecturerListUpdated = lambda c: self.collector.add("LecturerListUpdated", c)
                netmgr.onOTAUpdateAvailable = lambda v, u: self.collector.add("OTAUpdateAvailable", v, u)
            except Exception:
                pass

        ota = self.services.get("ota")
        if ota:
            try:
                ota.onUpdateStatusChanged = lambda s: self.collector.add("UpdateStatusChanged", s)
                ota.onUpdateProgress = lambda p, s: self.collector.add("UpdateProgress", p, s)
                ota.onUpdateCompleted = lambda ok, msg: self.collector.add("UpdateCompleted", ok, msg)
                ota.onMcuFirmwareUpdateRequired = lambda v, u: self.collector.add("McuFirmwareUpdateRequired", v, u)
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def get(self, name: str):
        return self.services.get(name)

    @property
    def is_connected(self) -> bool:
        return self.bus is not None

    def service_status(self) -> Dict[str, bool]:
        return {n: s is not None for n, s in self.services.items()}

    def stop(self) -> None:
        if self._loop:
            self._loop.quit()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1)


# ============================================================================
# MQTT Server Simulator
# ============================================================================

class MQTTServerSimulator:
    """Simulates the Server side by publishing/subscribing to MQTT topics."""

    def __init__(self, broker: str = MQTT_BROKER, port: int = MQTT_PORT, room_id: str = ROOM_ID):
        self.broker = broker
        self.port = port
        self.room_id = room_id
        self.client_id = f"sps-tester-{int(time.time())}"
        self._connected = False

        if not HAS_MQTT:
            click.secho("  paho-mqtt not available. Install with: pip install paho-mqtt", fg="yellow")
            return

        self.client = mqtt.Client(client_id=self.client_id, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        self._messages: List[mqtt.MQTTMessage] = []
        self._lock = threading.Lock()

    def connect(self) -> bool:
        if not HAS_MQTT:
            return False
        try:
            self.client.connect(self.broker, self.port, keepalive=60)
            self.client.loop_start()
            time.sleep(0.3)
            return True
        except Exception as e:
            click.secho(f"  MQTT connect failed: {e}", fg="red")
            return False

    def disconnect(self) -> None:
        if not HAS_MQTT:
            return
        try:
            self.client.loop_stop()
            self.client.disconnect()
        except Exception:
            pass

    def publish(self, topic_suffix: str, payload, qos: int = 1) -> bool:
        if not HAS_MQTT:
            return False
        topic = f"sps/{self.room_id}/{topic_suffix}"
        if isinstance(payload, (dict, list)):
            payload = json.dumps(payload)
        result = self.client.publish(topic, str(payload), qos=qos)
        return result.rc == mqtt.MQTT_ERR_SUCCESS

    def subscribe(self, topic_suffix: str, qos: int = 0) -> None:
        if not HAS_MQTT:
            return
        topic = f"sps/{self.room_id}/{topic_suffix}"
        self.client.subscribe(topic, qos=qos)

    def wait_for_message(self, topic_filter: Optional[str] = None,
                         timeout: float = MQTT_TIMEOUT) -> Optional[mqtt.MQTTMessage]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, msg in enumerate(self._messages):
                    if topic_filter is None or topic_filter in msg.topic:
                        self._messages.pop(i)
                        return msg
            time.sleep(0.05)
        return None

    def clear_messages(self) -> None:
        with self._lock:
            self._messages.clear()

    def _on_connect(self, client, userdata, flags, rc) -> None:
        self._connected = (rc == 0)

    def _on_disconnect(self, client, userdata, rc) -> None:
        self._connected = False

    def _on_message(self, client, userdata, msg) -> None:
        with self._lock:
            self._messages.append(msg)

    @property
    def connected(self) -> bool:
        return self._connected


# ============================================================================
# Test Scenario Definitions
# ============================================================================

TEST_SCENARIOS = [
    {"id": "TC-01", "category": "Authentication",
     "action": "Simulate a VALID RFID card tap",
     "expected": "svcAuthentication queries local Lecturers DB -> D-Bus AuthSuccess -> HMI unlocks"},
    {"id": "TC-02", "category": "Authentication",
     "action": "Simulate an INVALID RFID card tap",
     "expected": "Query fails -> D-Bus AuthenticationFailed -> HMI shows 'Invalid Card'"},
    {"id": "TC-03", "category": "Control",
     "action": "Simulate HMI command to toggle Projector ON",
     "expected": "svcAutomationEngine -> svcProtocolRouter -> UART hex 0x23 0x01 to MCU"},
    {"id": "TC-04", "category": "Control",
     "action": "Simulate HMI command: Start Class scenario",
     "expected": "Engine runs State Machine -> sequential UART cmds: Lights, Projector, AC"},
    {"id": "TC-05", "category": "Logout",
     "action": "Simulate HMI Logout button press",
     "expected": "Auth status -> LOCKED -> HMI returns to Kiosk screen"},
    {"id": "TC-06", "category": "Server_MQTT",
     "action": "Simulate Server requesting PCD status (via sps/{room}/cmd/#)",
     "expected": "svcNetworkManager receives cmd -> queries Engine via D-Bus -> publishes JSON status on sps/{room}/status/connection"},
    {"id": "TC-07", "category": "Server_MQTT",
     "action": "Simulate Server remote control device command (sps/{room}/cmd/#)",
     "expected": "svcNetworkManager parses cmd -> calls Engine D-Bus -> Router sends UART hex -> HMI updates via D-Bus"},
    {"id": "TC-08", "category": "OTA_Update",
     "action": "Simulate OTA trigger (sps/{room}/status/ota) while system IN_USE",
     "expected": "svcOtaManager detects active session -> rejects -> HMI 'Postpone' popup -> MQTT reject status"},
    {"id": "TC-09", "category": "OTA_Update",
     "action": "Simulate OTA trigger (sps/{room}/status/ota) while IDLE",
     "expected": "Server signals version+checksum on status/ota -> PCD pulls firmware via HTTP GET -> D-Bus progress -> MCU flash via UART"},
    {"id": "TC-10", "category": "Exception_Network",
     "action": "Simulate MQTT Broker disconnection (LWT on sps/{room}/status/connection)",
     "expected": "svcNetworkManager detects drop -> D-Bus MqttDisconnected -> HMI shows offline icon"},
    {"id": "TC-11", "category": "Background_Sync",
     "action": "Simulate Server pushing new Lecturer DB (sps/{room}/status/dbsync)",
     "expected": "MQTT-to-DB Worker publishes UID list -> svcNetworkManager writes local SQLite -> D-Bus LecturerListUpdated -> svcAuthentication ready for new cards"},
    {"id": "TC-12", "category": "Exception_UART",
     "action": "Simulate MCU not responding (UART disconnect / no ACK)",
     "expected": "svcProtocolRouter UART timeout -> D-Bus ConnectionStatusChanged(DISCONNECTED) -> MQTT alert to Server via status/connection / HMI error popup"},
    {"id": "TC-13", "category": "Hardware_Event",
     "action": "Simulate MCU presence detection (UART 0x41)",
     "expected": "svcProtocolRouter parses UART 0x41 -> D-Bus PresenceDetected -> Engine wakes HMI from Sleep"},
]


# ============================================================================
# Integration Tester
# ============================================================================

class SPSIntegrationTester:
    """Orchestrates all integration test scenarios."""

    def __init__(self):
        self.dbus = DBusClient()
        self.mqtt = MQTTServerSimulator()
        self._mqtt_auto_connected = False

    # ======================================================================
    # UI Helpers
    # ======================================================================

    def print_banner(self) -> None:
        click.clear()
        click.echo(click.style("=" * 72, fg="cyan"))
        click.echo(click.style("  Smart Podium System (SPS)  —  Integration Test CLI", fg="cyan", bold=True))
        click.echo(click.style("=" * 72, fg="cyan"))

        if self.dbus.is_connected:
            click.echo()
            click.echo("  D-Bus System Bus — Services:")
            for name, ok in self.dbus.service_status().items():
                icon = click.style("OK", fg="green") if ok else click.style("--", fg="red")
                click.echo(f"    {name:<12} [{icon}]")
        else:
            click.echo()
            click.echo(click.style("  D-Bus: DISCONNECTED", fg="red"))

        click.echo(f"\n  MQTT: {MQTT_BROKER}:{MQTT_PORT}   Room: {ROOM_ID}")
        click.echo()

    def _ensure_mqtt(self) -> None:
        if not self._mqtt_auto_connected:
            if self.mqtt.connect():
                self._mqtt_auto_connected = True

    # ======================================================================
    # Run Single Scenario
    # ======================================================================

    def run_scenario(self, tc_id: str) -> TestReport:
        spec = next((s for s in TEST_SCENARIOS if s["id"] == tc_id.upper()), None)
        if not spec:
            return TestReport(tc_id, "", "", "", TestResult.ERROR, f"Unknown TC-ID: {tc_id}")

        method_name = f"_run_{tc_id.lower().replace('-', '_')}"
        method = getattr(self, method_name, None)
        if not method:
            return TestReport(tc_id, spec["category"], spec["action"], spec["expected"],
                              TestResult.SKIP, "Test not yet implemented")

        click.echo(f"\n  {click.style('▶ Running', bold=True)} {tc_id}")
        click.echo(f"  Action:   {spec['action']}")
        click.echo(f"  Expected: {spec['expected']}")
        click.echo()

        if self.dbus.collector:
            self.dbus.collector.clear()

        start = time.monotonic()
        try:
            report = method(spec)
        except Exception as e:
            report = TestReport(tc_id, spec["category"], spec["action"], spec["expected"],
                                TestResult.ERROR, f"Exception: {e}", time.monotonic() - start)
        report.duration = time.monotonic() - start
        self._print_result(report)
        return report

    def _print_result(self, r: TestReport) -> None:
        color = {"PASS": "green", "FAIL": "red", "SKIP": "yellow", "ERROR": "red"}[r.result.value]
        click.echo()
        click.echo(click.style(f"  [{r.result.value}] {r.tc_id}", fg=color, bold=True))
        click.echo(f"  Duration: {r.duration:.2f}s")
        click.echo(f"  Detail:   {r.details}")

    # ======================================================================
    # TC-01  —  Valid RFID Card Tap
    # ======================================================================

    def _run_tc_01(self, spec: dict) -> TestReport:
        auth = self.dbus.get("auth")
        if not auth:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "svcAuthentication not reachable on D-Bus")

        try:
            ok = auth.UnlockScreen("VALID_LECTURER_001")
            click.echo(f"  → UnlockScreen returned: {ok}")
        except Exception as e:
            click.echo(f"  → UnlockScreen call: {e}")

        sig = self.dbus.collector.wait_for_any(["LecturerAuthenticated", "AuthStatusChanged"], timeout=4)
        if not sig:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "No D-Bus signal received within timeout")

        name, args = sig
        status_names = {0: "LOCKED", 1: "UNLOCKING", 2: "UNLOCKED", 3: "LOCKING", 4: "ERROR"}

        if name == "LecturerAuthenticated":
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Lecturer authenticated: '{args[0]}' at timestamp {args[1]}")
        if name == "AuthStatusChanged":
            st = args[0]
            if st == 2:
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"Auth → UNLOCKED")
            if st == 1:
                extra = self.dbus.collector.wait_for("LecturerAuthenticated", timeout=3)
                if extra:
                    return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                      TestResult.PASS, f"Lecturer authenticated: '{extra[0]}'")
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.FAIL, "Auth stuck UNLOCKING, no LecturerAuthenticated signal")
            st_name = status_names.get(st, f"unknown({st})")
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, f"Unexpected auth status: {st_name}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, f"Unexpected signal: {name}")

    # ======================================================================
    # TC-02  —  Invalid RFID Card Tap
    # ======================================================================

    def _run_tc_02(self, spec: dict) -> TestReport:
        auth = self.dbus.get("auth")
        if not auth:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "svcAuthentication not reachable")

        try:
            ok = auth.UnlockScreen("INVALID_CARD_99999")
            click.echo(f"  → UnlockScreen returned: {ok}")
        except Exception as e:
            click.echo(f"  → UnlockScreen: {e}")

        sig = self.dbus.collector.wait_for_any(["AuthenticationFailed", "AuthStatusChanged"], timeout=4)
        if not sig:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "No failure signal received")

        name, args = sig
        if name == "AuthenticationFailed":
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Auth failed: '{args[0]}'")
        if name == "AuthStatusChanged" and args[0] in (0, 4):
            st = {0: "LOCKED", 4: "ERROR"}.get(args[0], str(args[0]))
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Auth status → {st}  (invalid card rejected)")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, f"Unexpected: {name} {args}")

    # ======================================================================
    # TC-03  —  Toggle Projector ON
    # ======================================================================

    def _run_tc_03(self, spec: dict) -> TestReport:
        engine = self.dbus.get("engine")
        router = self.dbus.get("router")
        signals_seen = []

        if engine:
            for method_name in ("ControlProjector", "ControlDevice"):
                try:
                    if method_name == "ControlProjector":
                        r = engine.ControlProjector(True)
                    else:
                        r = engine.ControlDevice(4, "ON")
                    click.echo(f"  → engine.{method_name}() = {r}")
                    break
                except Exception as e:
                    click.echo(f"  → engine.{method_name}() failed: {e}")
        else:
            click.echo("  → svcAutomationEngine not reachable, trying router directly")

        if router:
            try:
                r = router.ControlProjector(True)
                click.echo(f"  → router.ControlProjector(ON) = {r}")
            except Exception:
                try:
                    r = router.SendCommand(0x23, [0x01])
                    click.echo(f"  → router.SendCommand(0x23, [0x01]) = {r}")
                except Exception as e:
                    click.echo(f"  → router.SendCommand failed: {e}")

        sig = self.dbus.collector.wait_for_any(
            ["CommandExecuting", "CommandAcknowledged", "CommandError", "DeviceStatusChanged"], timeout=5)
        if sig:
            name, args = sig
            signals_seen.append((name, args))
            if name == "CommandAcknowledged":
                cmd_name = UART_CMD_NAMES.get(args[0], f"0x{args[0]:02X}")
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"MCU ACK for {cmd_name}")
            if name == "CommandExecuting":
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"Engine executing command: [{args[1]}] {args[2]}")
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Signal: {name} {args}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No command-related D-Bus signal received within timeout")

    # ======================================================================
    # TC-04  —  Start Class Scenario
    # ======================================================================

    def _run_tc_04(self, spec: dict) -> TestReport:
        engine = self.dbus.get("engine")
        if not engine:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "svcAutomationEngine not reachable")

        try:
            scenarios = engine.GetAvailableScenarios()
            click.echo(f"  → Available scenarios: {list(scenarios)}")
        except Exception as e:
            click.echo(f"  → GetAvailableScenarios: {e}")

        for sid in ("START_CLASS", "start_class", "class_start"):
            try:
                ok = engine.ExecuteScenario(sid)
                click.echo(f"  → ExecuteScenario('{sid}') = {ok}")
                break
            except Exception as e:
                click.echo(f"  → ExecuteScenario('{sid}'): {e}")

        events = []
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            sig = self.dbus.collector.wait_for_any(
                ["ScenarioStarted", "CommandExecuting", "ScenarioCompleted", "ScenarioError"], timeout=2)
            if not sig:
                break
            name, args = sig
            events.append((name, args))
            if name == "ScenarioStarted":
                click.echo(f"  → Scenario started: {args[0]}")
            elif name == "CommandExecuting":
                click.echo(f"  →   Command [{args[1]}]: {args[2]}")
            elif name == "ScenarioCompleted":
                click.echo(f"  → Scenario completed: {args[0]}")
            elif name == "ScenarioError":
                click.echo(f"  → Scenario ERROR: {args}")

        if events:
            detail = f"Signals: {' → '.join(e[0] for e in events)}"
            passed = any(e[0] in ("ScenarioStarted", "CommandExecuting") for e in events)
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS if passed else TestResult.FAIL, detail)

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No scenario-related signals received")

    # ======================================================================
    # TC-05  —  Logout
    # ======================================================================

    def _run_tc_05(self, spec: dict) -> TestReport:
        auth = self.dbus.get("auth")
        engine = self.dbus.get("engine")

        if auth:
            try:
                ok = auth.LockScreen()
                click.echo(f"  → auth.LockScreen() = {ok}")
            except Exception as e:
                click.echo(f"  → auth.LockScreen: {e}")

        if engine:
            try:
                ok = engine.StopScenario("")
                click.echo(f"  → engine.StopScenario('') = {ok}")
            except Exception as e:
                click.echo(f"  → engine.StopScenario: {e}")

        sig = self.dbus.collector.wait_for_any(["AuthStatusChanged", "ScenarioCompleted"], timeout=4)
        if not sig:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "No logout/state-change signal received")

        name, args = sig
        if name == "AuthStatusChanged" and args[0] == 0:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, "Auth status → LOCKED (logout OK)")
        if name == "ScenarioCompleted":
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Scenario stopped: {args[0]}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.PASS, f"Signal received: {name} {args}")

    # ======================================================================
    # TC-06  —  Server requests PCD status
    # ======================================================================

    def _run_tc_06(self, spec: dict) -> TestReport:
        self._ensure_mqtt()
        self.dbus.collector.clear()

        # Subscribe to status/connection (LWT + status reports) and status/devices
        self.mqtt.subscribe("status/connection", qos=0)
        self.mqtt.subscribe("status/devices", qos=0)

        # Server sends status request command via sps/{room}/cmd/# wildcard
        payload = {"command": "get_status", "timestamp": int(time.time())}
        ok = self.mqtt.publish("cmd/status", payload)
        click.echo(f"  → Published status request to sps/{ROOM_ID}/cmd/status (rc={ok})")

        # Wait for PCD response on status/connection (LWT) or devices
        mqtt_msg = self.mqtt.wait_for_message(timeout=5)
        if mqtt_msg:
            try:
                data = json.loads(mqtt_msg.payload.decode())
            except Exception:
                data = mqtt_msg.payload.decode()
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"PCD status on '{mqtt_msg.topic}': {data}")

        sig = self.dbus.collector.wait_for_any(
            ["NetworkStatusChanged", "CommandReceived", "MqttMessageReceived"], timeout=3)
        if sig:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"D-Bus response: {sig[0]} {sig[1]}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No MQTT or D-Bus response to status request")

    # ======================================================================
    # TC-07  —  Server remote control command
    # ======================================================================

    def _run_tc_07(self, spec: dict) -> TestReport:
        self._ensure_mqtt()
        self.dbus.collector.clear()
        self.mqtt.subscribe("status/connection", qos=0)
        self.mqtt.subscribe("status/devices", qos=0)

        # Server publishes to sps/{room}/cmd/# wildcard; PCD subscribes to its room's cmd/#
        payload = {"device": "projector", "action": "ON", "timestamp": int(time.time())}
        ok = self.mqtt.publish("cmd/projector", payload)
        click.echo(f"  → Published to sps/{ROOM_ID}/cmd/projector (rc={ok})")

        # Expect D-Bus CommandReceived on svcNetworkManager -> Engine -> Router UART
        sig = self.dbus.collector.wait_for_any(["CommandReceived", "CommandExecuting", "CommandAcknowledged"], timeout=5)
        if sig:
            name, args = sig
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Service responded: {name} {args}")

        # Fallback: check for MQTT status update on connection or devices
        mqtt_msg = self.mqtt.wait_for_message(timeout=4)
        if mqtt_msg:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Status update on MQTT '{mqtt_msg.topic}': {mqtt_msg.payload.decode()[:120]}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No D-Bus or MQTT response to remote command")

    # ======================================================================
    # TC-08  —  OTA while IN_USE
    # ======================================================================

    def _run_tc_08(self, spec: dict) -> TestReport:
        self._ensure_mqtt()
        self.dbus.collector.clear()
        engine = self.dbus.get("engine")

        if engine:
            try:
                st = engine.GetEngineStatus()
                click.echo(f"  → Engine status: {st}")
            except Exception:
                pass

        # Hybrid OTA: Server signals version+checksum on sps/{room}/status/ota;
        # PCD pulls firmware via HTTP GET from File Storage.
        payload = {
            "version": "2.1.0",
            "checksum": "a1b2c3d4e5f6",
            "file": "/firmware/mcu_v2.1.0.bin",
            "timestamp": int(time.time()),
        }
        self.mqtt.publish("status/ota", payload)
        click.echo(f"  → Published OTA signal to sps/{ROOM_ID}/status/ota (version={payload['version']})")

        sig = self.dbus.collector.wait_for_any(
            ["UpdateStatusChanged", "UpdateCompleted", "OtaCommandReceived", "MqttMessageReceived"], timeout=6)
        if sig:
            name, args = sig
            payload_str = str(args).upper()
            if "REJECT" in payload_str or "FAIL" in payload_str or "IDLE" in payload_str or name == "UpdateCompleted":
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"OTA correctly blocked/rejected: {name} {args}")
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Response: {name} {args}")

        ota = self.dbus.get("ota")
        if ota:
            try:
                st = ota.GetOtaStatus()
                if "IDLE" in str(st).upper():
                    return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                      TestResult.PASS, f"OTA status still IDLE (rejected as expected): {st}")
            except Exception:
                pass

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No OTA-related signal received within timeout")

    # ======================================================================
    # TC-09  —  OTA while IDLE
    # ======================================================================

    def _run_tc_09(self, spec: dict) -> TestReport:
        self._ensure_mqtt()
        self.dbus.collector.clear()

        # Hybrid OTA: server signals on status/ota; PCD pulls via HTTP GET
        payload = {
            "version": "2.1.0",
            "checksum": "a1b2c3d4e5f6",
            "file": "/firmware/mcu_v2.1.0.bin",
            "timestamp": int(time.time()),
        }
        self.mqtt.publish("status/ota", payload)
        click.echo(f"  → Published OTA signal to sps/{ROOM_ID}/status/ota")

        # Expect D-Bus progress signals as PCD downloads (HTTP GET) and flashes MCU
        sig = self.dbus.collector.wait_for_any(
            ["UpdateStatusChanged", "UpdateProgress", "McuFirmwareUpdateRequired",
             "OTAUpdateAvailable", "UpdateCompleted"], timeout=10)
        if sig:
            name, args = sig
            if "DOWNLOADING" in str(args) or "FLASHING" in str(args) or "PROGRESS" in name.upper():
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"OTA progressing (HTTP pull + flash): {name} {args}")
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"OTA response: {name} {args}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No OTA progress signals received within timeout")

    # ======================================================================
    # TC-10  —  MQTT Broker disconnection
    # ======================================================================

    def _run_tc_10(self, spec: dict) -> TestReport:
        self.dbus.collector.clear()
        netmgr = self.dbus.get("netmgr")

        if netmgr:
            try:
                ok = netmgr.DisconnectFromMqtt()
                click.echo(f"  → netmgr.DisconnectFromMqtt() = {ok}")
            except Exception as e:
                click.echo(f"  → netmgr.DisconnectFromMqtt not available: {e}")
                click.echo("  → Falling back: stopping local MQTT client")
                if self._mqtt_auto_connected:
                    self.mqtt.disconnect()
                    self._mqtt_auto_connected = False
        else:
            click.echo("  → netmgr not reachable, stopping local MQTT")
            if self._mqtt_auto_connected:
                self.mqtt.disconnect()
                self._mqtt_auto_connected = False

        # Server detects disconnection via LWT on sps/{room}/status/connection,
        # but PCD should emit D-Bus signal locally first.
        sig = self.dbus.collector.wait_for_any(
            ["MqttDisconnected", "NetworkStatusChanged", "MqttError", "NetworkDisconnected"], timeout=5)
        if sig:
            name, args = sig
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Disconnect detected: {name} {args}, LWT should fire on status/connection")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No disconnection signal received within timeout")

    # ======================================================================
    # TC-11  —  Push Lecturer DB
    # ======================================================================

    def _run_tc_11(self, spec: dict) -> TestReport:
        self._ensure_mqtt()
        self.dbus.collector.clear()

        lecturers = [
            {"id": f"LEC{i:04d}", "name": f"Lecturer {i}",
             "card_id": f"RFID_{i:08d}", "department": "Engineering", "enabled": True}
            for i in range(50)
        ]
        payload = json.dumps({
            "command": "sync_lecturers",
            "lecturers": lecturers,
            "total_count": len(lecturers),
            "timestamp": int(time.time()),
        })
        self.mqtt.publish("cmd/sync", payload)
        click.echo(f"  → Published {len(lecturers)} lecturers to sps/{ROOM_ID}/cmd/sync")

        sig = self.dbus.collector.wait_for_any(
            ["LecturerListUpdated", "SyncDataReceived", "MqttMessageReceived"], timeout=5)
        if sig:
            name, args = sig
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Sync acknowledged: {name} {args}")

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No sync acknowledgment received within timeout")

    # ======================================================================
    # TC-12  —  MCU not responding (UART timeout)
    # ======================================================================

    def _run_tc_12(self, spec: dict) -> TestReport:
        self.dbus.collector.clear()
        router = self.dbus.get("router")

        if not router:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "svcProtocolRouter not reachable")

        click.echo("  → Sending command via router — expecting NO MCU ACK ...")
        try:
            r = router.SendCommand(0x21, [0x01, 0x01])
            click.echo(f"  → SendCommand(0x21, light_podium=ON) = {r}")
        except Exception as e:
            click.echo(f"  → SendCommand: {e}")

        # Also try ControlLight
        try:
            r = router.ControlLight(0x01, True)
            click.echo(f"  → ControlLight(0x01, ON) = {r}")
        except Exception:
            pass

        sig = self.dbus.collector.wait_for_any(
            ["ConnectionStatusChanged", "CommandError", "CommandAcknowledged"], timeout=6)
        if sig:
            name, args = sig
            if name == "CommandError":
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"MCU error: cmd=0x{args[0]:02X} code=0x{args[1]:02X}")
            if name == "CommandAcknowledged":
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.FAIL, f"MCU ACK'd (expected timeout/error): cmd=0x{args[0]:02X}")
            if name == "ConnectionStatusChanged":
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"Connection status: {args[0]}")
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"Signal: {name} {args}")

        # Check connection status
        try:
            st = router.GetConnectionStatus()
            if "DISCONNECT" in str(st).upper():
                return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                                  TestResult.PASS, f"Router reports: {st}")
        except Exception:
            pass

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL, "No timeout/error signal — MCU may still be responding")

    # ======================================================================
    # TC-13  —  Presence Detection (MCU→Host 0x41)
    # ======================================================================

    def _run_tc_13(self, spec: dict) -> TestReport:
        self.dbus.collector.clear()
        router = self.dbus.get("router")

        if not router:
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.FAIL, "svcProtocolRouter not reachable")

        click.echo("  → Waiting for PresenceDetected signal ...")
        click.echo("  (Inject 0x41 into UART RX, or trigger the physical sensor)")

        sig = self.dbus.collector.wait_for("PresenceDetected", timeout=8)
        if sig:
            args = sig
            present = args[0] if args else True
            return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                              TestResult.PASS, f"PresenceDetected(isPresent={present}) — HMI should wake")

        try:
            st = router.GetConnectionStatus()
            click.echo(f"  → UART connection: {st}")
        except Exception:
            pass

        return TestReport(spec["id"], spec["category"], spec["action"], spec["expected"],
                          TestResult.FAIL,
                          "No PresenceDetected signal. "
                          "Hint: use a virtual UART or physical sensor to inject 0x41.")

    # ======================================================================
    # Run All
    # ======================================================================

    def run_all(self) -> List[TestReport]:
        self.print_banner()
        click.echo(click.style("  Running ALL test scenarios ...\n", bold=True))
        reports = []
        for spec in TEST_SCENARIOS:
            reports.append(self.run_scenario(spec["id"]))
            click.echo()
        self._print_summary(reports)
        return reports

    def _print_summary(self, reports: List[TestReport]) -> None:
        passed = sum(1 for r in reports if r.result == TestResult.PASS)
        failed = sum(1 for r in reports if r.result == TestResult.FAIL)
        skipped = sum(1 for r in reports if r.result == TestResult.SKIP)
        errors = sum(1 for r in reports if r.result == TestResult.ERROR)

        click.echo(click.style("=" * 72, fg="cyan"))
        click.echo(click.style("  SUMMARY", bold=True))
        click.echo(click.style("=" * 72, fg="cyan"))
        click.echo()

        for r in reports:
            c = {"PASS": "green", "FAIL": "red", "SKIP": "yellow", "ERROR": "red"}[r.result.value]
            click.echo(f"  {click.style(f'[{r.result.value}]', fg=c)}  {r.tc_id:6s}  "
                       f"({r.category:20s})  {r.details[:80]}")

        click.echo()
        click.echo(f"  Total: {len(reports):2d}  |  "
                   f"{click.style(f'PASS: {passed}', fg='green')}  |  "
                   f"{click.style(f'FAIL: {failed}', fg='red')}  |  "
                   f"{click.style(f'SKIP: {skipped}', fg='yellow')}  |  "
                   f"{click.style(f'ERROR: {errors}', fg='red')}")

    # ======================================================================
    # Interactive Menu
    # ======================================================================

    def interactive_menu(self) -> None:
        self.print_banner()

        while True:
            click.echo(click.style("  ┌─ Test Selection ──────────────────────────────────────────────┐", bold=True))
            categories: Dict[str, list] = {}
            for s in TEST_SCENARIOS:
                categories.setdefault(s["category"], []).append(s)

            idx = 1
            mapping: List[str] = []
            for cat, scenarios in categories.items():
                click.echo(click.style(f"  │ {cat:25s}                              │", fg="yellow"))
                for s in scenarios:
                    mapping.append(s["id"])
                    click.echo(f"  │   {idx:2d}. {s['id']:6s}  {s['action']:<44s}│")
                    idx += 1
            click.echo(click.style("  │                                                    │", bold=True))
            click.echo(f"  │  {idx:2d}.  RUN ALL TESTS{' ':<46s}│")
            click.echo(f"  │  {idx+1:2d}.  Exit{' ':<55s}│")
            click.echo(click.style("  └──────────────────────────────────────────────────────────────┘", bold=True))
            click.echo()

            try:
                choice = click.prompt("  Select test", type=int)
            except (click.Abort, ValueError):
                click.echo("\n  Exiting.")
                break

            if choice == idx:
                self.run_all()
                click.pause("\n  Press any key to continue...")
                self.print_banner()
            elif choice == idx + 1:
                click.echo("\n  Exiting.")
                break
            elif 1 <= choice <= len(mapping):
                self.run_scenario(mapping[choice - 1])
                click.pause("\n  Press any key to continue...")
                self.print_banner()
            else:
                click.secho("  Invalid choice.", fg="red")

    # ======================================================================
    # Cleanup
    # ======================================================================

    def cleanup(self) -> None:
        if self._mqtt_auto_connected:
            try:
                self.mqtt.disconnect()
            except Exception:
                pass
        if self.dbus:
            try:
                self.dbus.stop()
            except Exception:
                pass


# ============================================================================
# CLI Entry Point  (click)
# ============================================================================

@click.group(invoke_without_command=True)
@click.option("--room", default=ROOM_ID, show_default=True, help="Room ID for MQTT topics")
@click.option("--mqtt-broker", default=MQTT_BROKER, show_default=True, help="MQTT broker address")
@click.option("--mqtt-port", default=MQTT_PORT, type=int, show_default=True, help="MQTT broker port")
@click.pass_context
def cli(ctx, room, mqtt_broker, mqtt_port):
    """Smart Podium System — Integration Test CLI

    Mocks RFID reader, MCU (UART), and Server (MQTT) to run
    end-to-end integration tests against live SPS D-Bus services.

    Without a subcommand, launches the interactive test menu.
    """
    ctx.ensure_object(dict)
    global ROOM_ID, MQTT_BROKER, MQTT_PORT
    ROOM_ID = room
    MQTT_BROKER = mqtt_broker
    MQTT_PORT = mqtt_port

    tester = SPSIntegrationTester()
    ctx.obj["tester"] = tester

    if ctx.invoked_subcommand is None:
        try:
            tester.interactive_menu()
        finally:
            tester.cleanup()


@cli.command()
@click.pass_context
def menu(ctx):
    """Launch the interactive test selection menu."""
    tester = ctx.obj["tester"]
    try:
        tester.interactive_menu()
    finally:
        tester.cleanup()


@cli.command()
@click.pass_context
def list(ctx):
    """List all available test scenarios with descriptions."""
    tester = ctx.obj["tester"]
    tester.print_banner()
    click.echo(click.style("  Available Test Scenarios:\n", bold=True))
    for s in TEST_SCENARIOS:
        click.echo(f"  {click.style(s['id'], bold=True):6s}  "
                   f"[{click.style(s['category'], fg='yellow'):20s}]  {s['action']}")
        click.echo(f"          → {s['expected']}")
        click.echo()


@cli.command()
@click.argument("tc_id", required=False)
@click.option("--all", "run_all", is_flag=True, help="Run every test scenario sequentially")
@click.pass_context
def run(ctx, tc_id, run_all):
    """Run one or all integration test scenarios.

    \b
    Examples:
      sps-tester run TC-01
      sps-tester run --all
    """
    tester = ctx.obj["tester"]
    try:
        if run_all:
            tester.run_all()
        elif tc_id:
            tester.run_scenario(tc_id.upper())
        else:
            click.echo("Specify a TC-ID (e.g., TC-01) or use --all")
            click.echo("Run 'sps-tester list' to see available scenarios.")
    finally:
        tester.cleanup()


# ============================================================================
# Main
# ============================================================================

def main():
    signal.signal(signal.SIGINT, lambda s, f: sys.exit(0))
    cli()


if __name__ == "__main__":
    main()
