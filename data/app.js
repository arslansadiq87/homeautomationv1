const tabs = document.querySelectorAll(".tab");
const settingsSubtabs = document.querySelectorAll(".settings-subtab");
const settingsSubpanels = document.querySelectorAll(".settings-subpanel");
const panels = {
  dashboard: document.querySelector("#dashboardPanel"),
  settings: document.querySelector("#settingsPanel"),
  radar: document.querySelector("#radarPanel"),
  fm225: document.querySelector("#fm225Panel"),
  rfid: document.querySelector("#rfidPanel")
};

const settingsForm = document.querySelector("#settingsForm");
const settingsMessage = document.querySelector("#settingsMessage");
const relaySettingsGrid = document.querySelector("#relaySettingsGrid");
const priorityRelayDashboardGrid = document.querySelector("#priorityRelayDashboardGrid");
const relayDashboardGrid = document.querySelector("#relayDashboardGrid");
const restartDeviceButton = document.querySelector("#restartDeviceButton");
const loadLogsButton = document.querySelector("#loadLogsButton");
const mp3Buttons = document.querySelectorAll(".mp3-action");
const fm225Buttons = document.querySelectorAll(".fm225-action");
const rfidButtons = document.querySelectorAll(".rfid-action");
const rs485TestSendButton = document.querySelector("#rs485TestSendButton");
const canTestSendButton = document.querySelector("#canTestSendButton");
const lockPulseTimers = {};
let latestRelayConfigs = [];
let statusRefreshInFlight = false;
let pendingStatusRefresh = false;
const relayActionInFlight = new Set();
const relayOptimisticState = new Map();

async function fetchWithTimeout(url, options = {}, timeoutMs = 1200) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, {
      ...options,
      signal: controller.signal
    });
  } finally {
    clearTimeout(timeout);
  }
}

function setText(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.textContent = value;
  }
}

function setValue(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.value = value;
  }
}

function setChecked(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.checked = Boolean(value);
  }
}

function setHidden(id, hidden) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.classList.toggle("hidden", hidden);
  }
}

function setCardState(id, enabled) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.dataset.state = enabled ? "on" : "off";
    element.classList.toggle("active", enabled);
  }
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  if (days > 0) {
    return `${days}d ${hours}h`;
  }
  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }
  return `${minutes}m`;
}

function formatNumber(value, decimals, suffix) {
  return Number.isFinite(value) ? `${value.toFixed(decimals)} ${suffix}` : "--";
}

function clampNumber(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function formatPower(value) {
  if (!Number.isFinite(value)) {
    return "-- W";
  }
  const abs = Math.abs(value);
  if (abs >= 1000) {
    return `${(value / 1000).toFixed(1)} kW`;
  }
  return `${Math.round(value).toLocaleString()} W`;
}

function formatEnergy(value) {
  return Number.isFinite(value) ? `${value.toFixed(1)} kWh` : "-- kWh";
}

function flowDuration(value) {
  const watts = Math.abs(value);
  if (!Number.isFinite(watts) || watts < 20) {
    return "0s";
  }
  const seconds = clampNumber(4.6 - Math.log10(watts + 10), 0.75, 3.4);
  return `${seconds.toFixed(2)}s`;
}

function setFlowDot(id, active, value, reverse = false) {
  const dot = document.querySelector(`#${id}`);
  if (!dot) {
    return;
  }

  dot.classList.toggle("active", active);
  if (!active) {
    return;
  }

  const animation = dot.querySelector("animateMotion");
  if (!animation) {
    return;
  }

  const duration = flowDuration(value);
  const direction = reverse ? "1;0" : "0;1";
  if (dot.dataset.duration !== duration || dot.dataset.direction !== direction) {
    animation.setAttribute("dur", duration);
    animation.setAttribute("keyPoints", direction);
    animation.setAttribute("keyTimes", "0;1");
    animation.setAttribute("calcMode", "linear");
    dot.dataset.duration = duration;
    dot.dataset.direction = direction;
    if (typeof animation.beginElement === "function") {
      animation.beginElement();
    }
  }
}

function formatI2cAddress(address) {
  return address ? `0x${address.toString(16).toUpperCase().padStart(2, "0")}` : "--";
}

function formatHex24(value) {
  return value ? `0x${value.toString(16).toUpperCase().padStart(6, "0")}` : "--";
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return "--";
  }

  const units = ["B", "KB", "MB", "GB"];
  let value = bytes;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  const decimals = unitIndex === 0 || value >= 10 ? 0 : 1;
  return `${value.toFixed(decimals)} ${units[unitIndex]}`;
}

function formatState(active, activeLabel, idleLabel) {
  return active ? activeLabel : idleLabel;
}

function updateConnection(status, label) {
  setText("esp32NetworkSummary", label || (status === "online" ? "Controller API online" : "Controller API offline"));
  setText("esp32WifiStatus", status === "online" ? "Online" : "Offline");
}

function updateEsp32Status(status) {
  const esp32Temperature = Number.isFinite(status.esp32Temperature)
    ? status.esp32Temperature
    : status.internalTemperature;
  const formattedTemperature = formatNumber(esp32Temperature, 1, "C");
  const wifiOnline = Boolean(status.wifiConnected);

  setText("esp32HeaderTemperature", formattedTemperature);
  setText("esp32AboutTemperature", formattedTemperature);
  setText("esp32Firmware", status.firmware || "--");
  setText("esp32NetworkSummary", wifiOnline ? `${status.ip || "--"} on WiFi` : "WiFi offline");
  setText("esp32WifiStatus", wifiOnline ? "Online" : "Offline");
  setText("esp32IpAddress", status.ip || "--");
  setText("esp32Rssi", Number.isFinite(status.rssi) ? `${status.rssi} dBm` : "--");
  setText("esp32FreeHeap", formatBytes(status.freeHeap));
  setText("esp32Uptime", formatUptime(status.uptimeSeconds || 0));
  setText("esp32StorageStatus", status.storageOnline ? "Online" : "Offline");
}

function updateMp3Status(status) {
  const file = status.mp3File || status.file || 0;
  const folder = status.mp3Folder || status.folder || 1;
  const totalFiles = status.mp3TotalFiles || status.totalFiles || 255;
  const volume = status.mp3Volume ?? status.volume;
  const initialized = Boolean(status.mp3Initialized ?? status.initialized);
  const playing = Boolean(status.mp3Playing ?? status.playing);

  setText("mp3PlayerState", initialized ? formatState(playing, "Playing", "Idle") : "Offline");
  setText("mp3CurrentTrack", `Folder ${folder}, track ${file || "--"} / ${totalFiles}`);
  if (Number.isFinite(volume)) {
    setValue("mp3LiveVolumeInput", volume);
    setText("mp3LiveVolumeValue", volume);
  }
}

function formatAge(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
    return "--";
  }
  if (milliseconds < 1000) {
    return `${milliseconds} ms`;
  }
  return `${Math.round(milliseconds / 1000)} s`;
}

function formatDistance(value) {
  return Number.isFinite(value) && value > 0 ? `${value} cm` : "-- cm";
}

function updateRadarStatus(status) {
  const online = Boolean(status.radarOnline);
  const present = Boolean(status.radarPresent);
  const moving = Boolean(status.radarMovingTarget);
  const stationary = Boolean(status.radarStationaryTarget);
  const state = status.radarState || "unknown";

  setText("radarState", online ? state.replaceAll("_", " ") : "Waiting");
  setText("radarStatus", online ? "LD2410B data received" : "UART on RX GPIO40, TX GPIO41");
  setText("radarPresence", online ? formatState(present, "Present", "Clear") : "--");
  setText("radarDashboardState", online ? formatState(present, "Motion", "Clear") : "--");
  setText(
    "radarTargetSummary",
    online ? `Detection distance ${formatDistance(status.radarDetectionDistanceCm)}` : "No radar data yet"
  );
  setText(
    "radarDashboardStatus",
    online
      ? `${state.replaceAll("_", " ")}, ${formatDistance(status.radarDetectionDistanceCm)}`
      : "LD2410B waiting for data"
  );
  setText("radarMovingDistance", formatDistance(status.radarMovingDistanceCm));
  setText("radarMovingEnergy", `Energy ${Number.isFinite(status.radarMovingEnergy) ? status.radarMovingEnergy : "--"}`);
  setText("radarStationaryDistance", formatDistance(status.radarStationaryDistanceCm));
  setText(
    "radarStationaryEnergy",
    `Energy ${Number.isFinite(status.radarStationaryEnergy) ? status.radarStationaryEnergy : "--"}`
  );
  setText("radarDetectionDistance", formatDistance(status.radarDetectionDistanceCm));
  setText("radarMovingState", formatState(moving, "Detected", "Clear"));
  setText("radarStationaryState", formatState(stationary, "Detected", "Clear"));
  setText("radarEngineeringMode", formatState(status.radarEngineeringMode, "Enabled", "Off"));
  setText("radarLastUpdate", online ? formatAge(status.radarLastUpdateAgeMs) : "--");
}

function updateTdsStatus(status) {
  const enabled = Boolean(status.tdsMonitorEnabled);
  const online = Boolean(status.tdsMonitorOnline);
  const card = document.querySelector("#tdsMonitorCard");
  const tdsGauge = document.querySelector("#tdsGauge");
  const waterGauge = document.querySelector("#tdsWaterGauge");
  setHidden("tdsMonitorCard", !enabled);
  if (!enabled) {
    return;
  }

  const ppm = status.tdsPpm;
  const waterLevel = status.tdsWaterLevelPercent;
  const ppmValue = Number.isFinite(ppm) ? clampNumber(ppm, 0, 1000) : 0;
  const waterValue = Number.isFinite(waterLevel) ? clampNumber(waterLevel, 0, 100) : 0;

  setText("tdsMonitorStatus", online ? "Live" : "Offline");
  setText("tdsPpm", Number.isFinite(ppm) ? `${Math.round(ppm)}` : "--");
  setText("tdsWaterLevel", Number.isFinite(waterLevel) ? `${Math.round(waterValue)}%` : "--%");
  setCardState("tdsMonitorCard", online);

  if (card) {
    card.style.setProperty("--tds-gauge", `${(ppmValue / 1000) * 360}deg`);
    card.style.setProperty("--tds-level", `${waterValue}%`);
  }
  if (tdsGauge) {
    if (Number.isFinite(ppm)) {
      tdsGauge.setAttribute("aria-valuenow", `${Math.round(ppmValue)}`);
      tdsGauge.setAttribute("aria-valuetext", `${Math.round(ppm)} ppm`);
    } else {
      tdsGauge.removeAttribute("aria-valuenow");
      tdsGauge.setAttribute("aria-valuetext", "TDS unavailable");
    }
  }
  if (waterGauge) {
    if (Number.isFinite(waterLevel)) {
      waterGauge.setAttribute("aria-valuenow", `${Math.round(waterValue)}`);
      waterGauge.setAttribute("aria-valuetext", `${Math.round(waterValue)} percent`);
    } else {
      waterGauge.removeAttribute("aria-valuenow");
      waterGauge.setAttribute("aria-valuetext", "Water level unavailable");
    }
  }
}

function updateAirPurifierStatus(status) {
  const enabled = Boolean(status.airPurifierEnabled);
  const online = Boolean(status.airPurifierOnline);
  const pm25 = status.airPurifierPm25;
  const pm1 = status.airPurifierPm1;
  const pm10 = status.airPurifierPm10;
  const card = document.querySelector("#airPurifierCard");
  const gauge = document.querySelector("#airPurifierGauge");
  setHidden("airPurifierCard", !enabled);
  if (!enabled) {
    return;
  }

  const pm25Value = Number.isFinite(pm25) ? clampNumber(pm25, 0, 500) : 0;
  setText("airPurifierStatus", online ? (status.airPurifierOn ? "Purifying" : "Online") : status.airPurifierLastEvent || "Offline");
  setText("airPurifierPm25", Number.isFinite(pm25) ? `${Math.round(pm25)}` : "--");
  setText("airPurifierPm1", Number.isFinite(pm1) ? `${Math.round(pm1)}` : "--");
  setText("airPurifierPm10", Number.isFinite(pm10) ? `${Math.round(pm10)}` : "--");
  setText("airPurifierFan", status.airPurifierFanOn ? `${status.airPurifierFanSpeedPct || 0}%` : "Off");
  setText("airPurifierPower", status.airPurifierOn ? "ON" : "OFF");
  setCardState("airPurifierCard", online && status.airPurifierOn);
  if (card) {
    card.style.setProperty("--air-quality", `${(pm25Value / 500) * 360}deg`);
  }
  if (gauge) {
    if (Number.isFinite(pm25)) {
      gauge.setAttribute("aria-valuenow", `${Math.round(pm25Value)}`);
      gauge.setAttribute("aria-valuetext", `${Math.round(pm25)} PM2.5`);
    } else {
      gauge.removeAttribute("aria-valuenow");
      gauge.setAttribute("aria-valuetext", "PM2.5 unavailable");
    }
  }
}

function updateCommunicationStatus(status) {
  setText("rs485StatusText", status.rs485Enabled ? status.rs485Status || "Ready" : "Disabled");
  setText("rs485RxPinText", `GPIO${status.rs485RxPin ?? 15}`);
  setText("rs485TxPinText", `GPIO${status.rs485TxPin ?? 18}`);
  setText("rs485DirectionPinText", `GPIO${status.rs485DirectionPin ?? 48}`);
  setText("canStatusText", status.canEnabled ? status.canStatus || "Ready" : "Disabled");
  setText("canTxPinText", `GPIO${status.canTxPin ?? 5}`);
  setText("canRxPinText", `GPIO${status.canRxPin ?? 6}`);
}

function updateInverterCard(prefix, status, label) {
  const enabled = status[`${prefix}Enabled`] !== false;
  setHidden(`${prefix}InverterCard`, !enabled);
  if (!enabled) {
    setFlowDot(`${prefix}PvInverterDot`, false, 0);
    setFlowDot(`${prefix}GridInverterDot`, false, 0);
    setFlowDot(`${prefix}InverterHomeDot`, false, 0);
    setFlowDot(`${prefix}BatteryInverterDot`, false, 0);
    return;
  }
  const online = Boolean(status[`${prefix}Online`]);
  const pv = status[`${prefix}PvPowerW`];
  const grid = status[`${prefix}GridPowerW`];
  const gridVoltage = status[`${prefix}GridVoltageV`];
  const home = status[`${prefix}HomePowerW`];
  const battery = status[`${prefix}BatteryPowerW`];
  const batterySoc = status[`${prefix}BatterySoc`];
  const eventText = status[`${prefix}LastEvent`] || "Waiting for inverter data";

  setText(`${prefix}InverterStatus`, enabled ? (online ? "Live" : eventText) : "Disabled");
  setText(`${prefix}PvPower`, formatPower(pv));
  setText(`${prefix}GridVoltage`, Number.isFinite(gridVoltage) ? `${Math.round(gridVoltage)} V` : "-- V");
  setText(`${prefix}GridPower`, formatPower(grid));
  setText(`${prefix}HomePower`, formatPower(home));
  setText(`${prefix}HomeImport`, formatPower(home));
  setText(`${prefix}BatterySoc`, Number.isFinite(batterySoc) ? `${Math.round(batterySoc)} %` : "-- %");
  setText(`${prefix}BatteryPower`, formatPower(battery));
  setCardState(`${prefix}InverterCard`, enabled && online);

  const active = enabled && online;
  const pvActive = active && Number.isFinite(pv) && pv > 20;
  const gridImport = active && Number.isFinite(grid) && grid > 20;
  const gridExport = active && Number.isFinite(grid) && grid < -20;
  const batteryCharging = active && Number.isFinite(battery) && battery < -20;
  const batteryDischarging = active && Number.isFinite(battery) && battery > 20;

  setFlowDot(`${prefix}PvInverterDot`, pvActive, pv);
  setFlowDot(`${prefix}GridInverterDot`, gridImport || gridExport, Math.abs(grid), gridExport);
  setFlowDot(`${prefix}InverterHomeDot`, active && Number.isFinite(home) && home > 20, home);
  setFlowDot(`${prefix}BatteryInverterDot`, batteryCharging || batteryDischarging, Math.abs(battery), batteryCharging);
}

function updateInverterStatus(status) {
  updateInverterCard("nitrox", status, "Nitrox");
  updateInverterCard("solax", status, "Solax");
  updateGrowattCard(status);
}

function updateGrowattCard(status) {
  const enabled = status.growattEnabled !== false;
  setHidden("growattInverterCard", !enabled);
  if (!enabled) {
    return;
  }
  const online = Boolean(status.growattOnline);
  const eventText = status.growattLastEvent || "Waiting for Growatt data";

  setText("growattInverterStatus", enabled ? (online ? "Live" : eventText) : "Disabled");
  setText("growattPvPower", formatPower(status.growattPvPowerW));
  setText("growattGridPower", formatPower(status.growattGridPowerW));
  setText("growattGridVoltage", online ? "On Grid" : "--");
  setText("growattTodayEnergy", formatEnergy(status.growattTodayYieldKwh));
  setText("growattTotalEnergy", formatEnergy(status.growattTotalEnergyKwh));
  setCardState("growattInverterCard", enabled && online);
}

function renderFm225UserTable(users) {
  const body = document.querySelector("#fm225UserTableBody");
  if (!body) {
    return;
  }

  body.textContent = "";
  if (!users.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.textContent = "No users loaded";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  users.forEach((user, index) => {
    const row = document.createElement("tr");
    const numberCell = document.createElement("td");
    const idCell = document.createElement("td");
    const nameCell = document.createElement("td");
    const actionCell = document.createElement("td");
    const getButton = document.createElement("button");
    const deleteButton = document.createElement("button");
    const actions = document.createElement("div");

    numberCell.textContent = `${index + 1}`;
    idCell.textContent = `${user.id}`;
    nameCell.textContent = user.name || "N/A";
    actions.className = "table-action-row";

    getButton.className = "icon-action";
    getButton.type = "button";
    getButton.textContent = "Get";
    getButton.addEventListener("click", () => {
      runFm225Action("get-user", user.id);
    });

    deleteButton.className = "icon-action danger-action";
    deleteButton.type = "button";
    deleteButton.textContent = "Delete";
    deleteButton.addEventListener("click", () => {
      runFm225Action("delete-user", user.id);
    });

    actions.append(getButton, deleteButton);
    actionCell.appendChild(actions);
    row.append(numberCell, idCell, nameCell, actionCell);
    body.appendChild(row);
  });
}

function updateFm225Status(status) {
  const responding = Boolean(status.fm225Responding);
  const statusText = status.fm225Status || "UART ready";
  const eventText = status.fm225LastEvent || "Waiting for FM225 response";
  const radarText = status.fm225RadarPresenceEnabled ? status.fm225RadarStatus || "Radar presence waiting" : "";
  const users = Array.isArray(status.fm225Users) ? status.fm225Users : [];
  const recognizedName = status.fm225LastRecognizedName || "";
  const recognizedId = status.fm225LastRecognizedUserId || 0;
  const verifySummary = status.fm225VerifySummary || "Verification not started";

  setText("fm225DashboardState", responding ? statusText : "UART");
  setText("fm225DashboardStatus", radarText || (responding ? eventText : "FM225 waiting for response"));
  setText("fm225State", responding ? statusText : "UART Ready");
  setText(
    "fm225Status",
    `RX GPIO38, TX GPIO37${status.fm225Version ? `, ${status.fm225Version}` : ""}${
      status.fm225SerialNumber ? `, SN ${status.fm225SerialNumber}` : ""
    }`
  );
  setText("fm225Recognized", recognizedId ? `ID ${recognizedId}` : "--");
  setText("fm225VerifyStatus", radarText || verifySummary);
  setText("fm225FaceState", status.fm225FaceUpdated ? status.fm225FaceState : "--");
  setText("fm225FaceVerifyResult", radarText || verifySummary);
  setText("fm225FacePose", `Yaw ${status.fm225FaceYaw || 0}, pitch ${status.fm225FacePitch || 0}, roll ${status.fm225FaceRoll || 0}`);
  setText("fm225UserCount", `${status.fm225UserCount || 0}`);
  renderFm225UserTable(users);
  setText("fm225EnrollState", Number.isFinite(status.fm225LastEnrollResult) && status.fm225LastEnrollResult === 0 ? "Complete" : "Ready");
  setText("fm225LastEvent", radarText || eventText);
}

function renderTagTable(tags) {
  const body = document.querySelector("#rfidTagTableBody");
  if (!body) {
    return;
  }

  body.textContent = "";
  if (!tags.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.textContent = "No authorized tags";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  tags.forEach((tag, index) => {
    const row = document.createElement("tr");
    const numberCell = document.createElement("td");
    const tagCell = document.createElement("td");
    const addedCell = document.createElement("td");
    const actionCell = document.createElement("td");
    const deleteButton = document.createElement("button");

    numberCell.textContent = `${index + 1}`;
    tagCell.textContent = tag;
    addedCell.textContent = "N/A";
    deleteButton.className = "icon-action danger-action rfid-delete-tag";
    deleteButton.type = "button";
    deleteButton.textContent = "Delete";
    deleteButton.dataset.tag = tag;
    deleteButton.addEventListener("click", () => {
      runRfidAction("delete", tag);
    });

    actionCell.appendChild(deleteButton);
    row.append(numberCell, tagCell, addedCell, actionCell);
    body.appendChild(row);
  });
}

function updateRfidAddModeUI(active, pendingTags, remainingMs) {
  setHidden("rfidAddTagButton", active);
  setHidden("rfidSaveTagsButton", !active);
  setHidden("rfidCancelAddButton", !active);
  setHidden("rfidPendingPanel", !active);

  if (active) {
    const seconds = Math.ceil((remainingMs || 0) / 1000);
    setText("rfidAddModeMessage", `Scan RFID tags to add${seconds > 0 ? ` (${seconds}s)` : ""}`);
    setText("rfidPendingTagList", pendingTags.length ? pendingTags.join(", ") : "No pending tags");
  }
}

function updateRfidStatus(status) {
  const initialized = Boolean(status.rfidInitialized);
  const tagPresent = Boolean(status.rfidTagPresent);
  const authorized = Boolean(status.rfidLastAuthorized);
  const tags = Array.isArray(status.rfidTags) ? status.rfidTags : [];
  const pendingTags = Array.isArray(status.rfidPendingTags) ? status.rfidPendingTags : [];
  const addModeActive = Boolean(status.rfidAddModeActive);
  const lastTag = status.rfidLastTag || "";
  const state = !initialized ? "Offline" : addModeActive ? "Add Mode" : tagPresent ? formatState(authorized, "Allowed", "Denied") : "Ready";
  const event = status.rfidLastEvent || "No tag scanned";

  setText("rfidDashboardState", state);
  setText("rfidDashboardStatus", addModeActive ? "Scan RFID tags to add" : lastTag ? `${lastTag} - ${event}` : "RDM6300 on GPIO17");
  setText("rfidState", state);
  setText("rfidStatus", addModeActive ? "Add mode active: scan tags" : initialized ? "RX GPIO17, 9600 baud" : "RFID reader offline");
  setText("rfidLastTag", lastTag || "--");
  setText("rfidLastEvent", event);
  setText("rfidTagCount", `${status.rfidTagCount || 0}`);
  setText("rfidReadCount", `Reads ${status.rfidTotalReads || 0}`);
  setText("rfidActionState", addModeActive ? "Add mode active: scan tags" : event || "Ready");
  updateRfidAddModeUI(addModeActive, pendingTags, status.rfidAddModeRemainingMs);
  renderTagTable(tags);
}

function modeLabel(mode) {
  if (mode === "pulse") {
    return "Pulse";
  }
  return mode === "automatic" ? "Automatic" : "Manual";
}

function relayStateLabel(enabled) {
  return enabled ? "ON" : "OFF";
}

function doorStatusLabel(closed) {
  return closed ? "Door closed" : "Door opened";
}

function formatDurationMs(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
    return "0s";
  }
  if (milliseconds < 1000) {
    return `${milliseconds}ms`;
  }
  return `${Math.round(milliseconds / 1000)}s`;
}

function setLockCardPulsing(cardId, pulsing, remainingMs = 0) {
  const card = document.querySelector(`#${cardId}`);
  if (!card) {
    return;
  }

  if (lockPulseTimers[cardId]) {
    clearTimeout(lockPulseTimers[cardId]);
    lockPulseTimers[cardId] = null;
  }

  card.classList.toggle("pulsing", pulsing);
  card.classList.toggle("busy", pulsing);

  if (pulsing && remainingMs > 0) {
    lockPulseTimers[cardId] = setTimeout(() => {
      card.classList.remove("pulsing", "busy");
      refreshStatus();
    }, remainingMs);
  }
}

function updateAutomationStatus(status) {
  if (Array.isArray(status.relayConfigs)) {
    latestRelayConfigs = status.relayConfigs;
    renderDashboardRelays(status);
  }
}

function minutesToTime(value) {
  const minutes = Number.isFinite(Number(value)) ? Number(value) : 0;
  return `${String(Math.floor(minutes / 60)).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
}

function timeToMinutes(value) {
  const [hours, minutes] = String(value || "00:00").split(":").map((part) => Number(part));
  return clampNumber((hours || 0) * 60 + (minutes || 0), 0, 1439);
}

function sensorLabel(sensor) {
  return {
    indoorTemperature: "Inside temperature",
    outdoorTemperature: "Outside temperature",
    humidity: "Humidity",
    lux: "Lux",
    mq135: "MQ135",
    pir: "PIR motion"
  }[sensor] || "Sensor";
}

function comparisonLabel(comparison) {
  return {
    greaterThan: "Greater than",
    lessThan: "Less than",
    motionDetected: "Motion detected",
    noMotionDetected: "No motion detected"
  }[comparison] || "Greater than";
}

function relaySensorValue(config, status) {
  if (config.sensor === "lux") {
    return formatNumber(status.lux, 1, "lx");
  }
  if (config.sensor === "mq135") {
    return `Raw ${status.mq135AnalogRaw ?? "--"}`;
  }
  if (config.sensor === "pir") {
    return status.motion1Active || status.motion2Active ? "Motion" : "Quiet";
  }
  if (config.sensor === "outdoorTemperature") {
    return formatNumber(status.ds18b20Temperature, 1, "C");
  }
  if (config.sensor === "humidity") {
    return Number.isFinite(status.humidity) ? `${Math.round(status.humidity)}% RH` : "--";
  }
  return formatNumber(status.temperature, 1, "C");
}

function updatePulseRoleOptions() {
  const selects = Array.from(document.querySelectorAll('select[name$="PulseRole"]'));
  ["garageDoor", "garageGate"].forEach((role) => {
    const selected = selects.find((select) => select.value === role);
    selects.forEach((select) => {
      const option = Array.from(select.options).find((item) => item.value === role);
      if (option) {
        option.disabled = Boolean(selected && selected !== select);
      }
    });
  });
}

function dashboardRelayDetail(config, status) {
  if (config.pulseRole === "garageDoor") {
    return doorStatusLabel(Boolean(status.doorReedClosed));
  }
  if (config.pulseRole === "garageGate") {
    return doorStatusLabel(Boolean(status.garageReedClosed));
  }
  if (config.mode === "pulse") {
    return `Pulse ${formatDurationMs(config.pulseDurationMs)}`;
  }
  if (config.mode === "automatic" && config.automaticControlType === "schedule") {
    return `Schedule ${minutesToTime(config.scheduleOnMinutes)}-${minutesToTime(config.scheduleOffMinutes)}`;
  }
  if (config.mode === "automatic") {
    return `Automatic, ${sensorLabel(config.sensor)} ${relaySensorValue(config, status)}`;
  }
  return "Manual mode";
}

function renderDashboardRelays(status) {
  if (!relayDashboardGrid || !priorityRelayDashboardGrid) {
    return;
  }

  const visible = latestRelayConfigs.filter((config) => config.showInDashboard || config.pulseRole === "garageDoor" || config.pulseRole === "garageGate");
  const priorityRelays = visible.filter((config) => config.mode === "pulse" && (config.pulseRole === "garageDoor" || config.pulseRole === "garageGate"));
  const remainingRelays = visible.filter((config) => !priorityRelays.includes(config));
  const renderRelaySet = (container, configs) => {
    container.textContent = "";
    configs.forEach((config) => {
      container.append(createDashboardRelayCard(config, status));
    });
  };

  renderRelaySet(priorityRelayDashboardGrid, priorityRelays);
  renderRelaySet(relayDashboardGrid, remainingRelays);
}

function createDashboardRelayCard(config, status) {
  const channel = Number(config.channel);
  const active = relayOptimisticState.has(channel)
    ? relayOptimisticState.get(channel)
    : Boolean(status.relays?.[channel] ?? config.currentState);
  const isRolePulse = config.mode === "pulse" && (config.pulseRole === "garageDoor" || config.pulseRole === "garageGate");
  const card = document.createElement("article");
  card.className = `metric-card relay-card dashboard-relay-card ${isRolePulse ? "lock-card" : ""} ${
    config.mode === "automatic" ? "automatic-mode" : ""
  }`;
  card.id = `relayDashboardCard${channel}`;
  card.dataset.state = active ? "on" : "off";
  card.classList.toggle("busy", relayActionInFlight.has(channel));

  const meta = document.createElement("div");
  meta.className = "card-meta";
  const name = document.createElement("span");
  name.textContent = config.name || `Relay ${channel + 1}`;
  const state = document.createElement("strong");
  state.textContent = relayStateLabel(active);
  meta.append(name, state);

  const detail = document.createElement("p");
  if (isRolePulse) {
    detail.className = "door-status";
  }
  detail.textContent = dashboardRelayDetail(config, status);
  card.append(meta, detail);

  if (isRolePulse) {
    card.dataset.pulseMs = `${config.pulseDurationMs || 1000}`;
    card.classList.toggle("pulsing", active);
    card.classList.toggle("busy", active || relayActionInFlight.has(channel));
    if (active) {
      state.textContent = "UNLOCKING";
    }
    card.addEventListener("click", () => {
      if (!card.classList.contains("busy")) {
        pulseDashboardRelay(channel, card);
      }
    });
  } else if (config.mode === "manual") {
    card.classList.add("clickable-relay-card");
    card.setAttribute("role", "button");
    card.setAttribute("tabindex", "0");
    card.setAttribute("aria-label", `${active ? "Turn off" : "Turn on"} ${name.textContent}`);
    card.addEventListener("click", () => setDashboardRelay(channel, !active));
    card.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        setDashboardRelay(channel, !active);
      }
    });
  } else if (config.mode === "pulse") {
    card.classList.add("clickable-relay-card");
    card.setAttribute("role", "button");
    card.setAttribute("tabindex", active || relayActionInFlight.has(channel) ? "-1" : "0");
    card.setAttribute("aria-label", `Activate ${name.textContent}`);
    card.classList.toggle("busy", active || relayActionInFlight.has(channel));
    card.addEventListener("click", () => {
      if (!card.classList.contains("busy")) {
        pulseDashboardRelay(channel, card);
      }
    });
    card.addEventListener("keydown", (event) => {
      if ((event.key === "Enter" || event.key === " ") && !card.classList.contains("busy")) {
        event.preventDefault();
        pulseDashboardRelay(channel, card);
      }
    });
  } else {
    const auto = document.createElement("small");
    auto.textContent =
      config.automaticControlType === "schedule"
        ? `Days mask ${config.enabledWeekdays}`
        : `${comparisonLabel(config.comparison)}, ON ${config.onThreshold}, OFF ${config.offThreshold}`;
    card.append(auto);
  }

  return card;
}

function requestStatusRefresh(delayMs = 250) {
  window.setTimeout(refreshStatus, delayMs);
}

function markRelayCardBusy(channel, busy) {
  document.querySelectorAll(`#relayDashboardCard${channel}`).forEach((card) => {
    card.classList.toggle("busy", busy);
    if (card.classList.contains("clickable-relay-card")) {
      card.setAttribute("aria-busy", busy ? "true" : "false");
    }
  });
}

function setRelayCardLocalState(channel, enabled) {
  relayOptimisticState.set(channel, enabled);
  document.querySelectorAll(`#relayDashboardCard${channel}`).forEach((card) => {
    card.dataset.state = enabled ? "on" : "off";
    card.classList.toggle("active", enabled);
    const state = card.querySelector(".card-meta strong");
    if (state) {
      state.textContent = card.classList.contains("lock-card") && enabled ? "UNLOCKING" : relayStateLabel(enabled);
    }
    const name = card.querySelector(".card-meta span")?.textContent || `Relay ${channel + 1}`;
    if (card.classList.contains("clickable-relay-card") && !card.classList.contains("lock-card")) {
      card.setAttribute("aria-label", `${enabled ? "Turn off" : "Turn on"} ${name}`);
    }
  });
}

async function postRelayControl(endpoint, channel, enabled) {
  const payload = new URLSearchParams();
  payload.set("channel", channel);
  if (typeof enabled === "boolean") {
    payload.set("enabled", enabled ? "true" : "false");
  }
  const response = await fetchWithTimeout(`/api/relay/${endpoint}`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: payload,
    cache: "no-store"
  }, 2000);
  if (!response.ok) {
    throw new Error(`Relay action failed: ${response.status}`);
  }
  const result = await response.json();
  if (!result.ok) {
    throw new Error("Relay action rejected");
  }
  return result;
}

function setDashboardRelay(channel, enabled) {
  if (relayActionInFlight.has(channel)) {
    return;
  }
  relayActionInFlight.add(channel);
  const previousState = !enabled;
  setRelayCardLocalState(channel, enabled);
  markRelayCardBusy(channel, true);

  postRelayControl("manual", channel, enabled)
    .catch(() => {
      setRelayCardLocalState(channel, previousState);
      updateConnection("offline", "Relay failed");
    })
    .finally(() => {
      relayActionInFlight.delete(channel);
      markRelayCardBusy(channel, false);
      requestStatusRefresh(250);
      window.setTimeout(() => relayOptimisticState.delete(channel), 1200);
    });
}

function pulseDashboardRelay(channel, card = null) {
  if (relayActionInFlight.has(channel)) {
    return;
  }
  relayActionInFlight.add(channel);
  if (card) {
    const pulseMs = Number.parseInt(card.dataset.pulseMs || "1000", 10);
    setLockCardPulsing(card.id, true, Number.isFinite(pulseMs) ? pulseMs : 1000);
  }
  markRelayCardBusy(channel, true);
  setRelayCardLocalState(channel, true);

  postRelayControl("pulse", channel)
    .catch(() => {
      if (card) {
        setLockCardPulsing(card.id, false);
      }
      setRelayCardLocalState(channel, false);
      updateConnection("offline", "Relay failed");
    })
    .finally(() => {
      const pulseMs = card ? Number.parseInt(card.dataset.pulseMs || "1000", 10) : 1000;
      window.setTimeout(() => {
        relayActionInFlight.delete(channel);
        relayOptimisticState.delete(channel);
        markRelayCardBusy(channel, false);
        requestStatusRefresh(0);
      }, Number.isFinite(pulseMs) ? Math.min(Math.max(pulseMs, 300), 3000) : 1000);
    });
}

async function refreshStatus() {
  if (statusRefreshInFlight) {
    pendingStatusRefresh = true;
    return;
  }
  statusRefreshInFlight = true;
  pendingStatusRefresh = false;
  try {
    const response = await fetchWithTimeout("/api/status", { cache: "no-store" }, 1200);
    if (!response.ok) {
      throw new Error(`Status API failed: ${response.status}`);
    }

    const status = await response.json();
    const climateCard = document.querySelector("#climateSensorCard");
    const outdoorClimateCard = document.querySelector("#outdoorClimateCard");
    const climateGauge = document.querySelector("#climateGauge");
    const outdoorTempGauge = document.querySelector("#outdoorTempGauge");
    const lightGauge = document.querySelector("#lightGauge");
    const climateOnline = Boolean(status.sht3xOnline);
    const outdoorClimateOnline = Boolean(status.ds18b20Online || (status.bh1750Online && status.lightValid));
    const humidityValue = Number.isFinite(status.humidity) ? clampNumber(status.humidity, 0, 100) : 0;
    const lightValue = Number.isFinite(status.lux) ? clampNumber(status.lux, 0, 1000) : 0;
    const outdoorTempValue = Number.isFinite(status.ds18b20Temperature) ? clampNumber(status.ds18b20Temperature, -10, 60) : -10;

    setText("climateStatus", climateOnline ? "Live" : "Offline");
    setText("outdoorClimateStatus", outdoorClimateOnline ? "Live" : "Offline");
    setText("temperature", formatNumber(status.temperature, 1, "C"));
    setText("humidity", Number.isFinite(status.humidity) ? `${Math.round(humidityValue)}% RH` : "--% RH");
    setText("lux", formatNumber(status.lux, 1, "lx"));
    const smokeDetected = Boolean(status.mq135AlarmActive ?? status.mq135DigitalActive);
    const smokeThreshold = Number.isFinite(status.mq135AlarmThresholdRaw) ? status.mq135AlarmThresholdRaw : "--";
    setText("mq135Status", smokeDetected ? "Smoke detected" : "Clear");
    setText("mq135State", formatState(smokeDetected, "Alert", "Clear"));
    setText("mq135Reading", `Raw ${status.mq135AnalogRaw} / ${smokeThreshold} (${formatNumber(status.mq135AnalogVoltage, 2, "V")})`);
    setCardState("mq135Card", smokeDetected);
    setText("ds18b20Temperature", formatNumber(status.ds18b20Temperature, 1, "C"));
    setCardState("climateSensorCard", climateOnline);
    setCardState("outdoorClimateCard", outdoorClimateOnline);
    if (climateCard) {
      climateCard.style.setProperty("--climate-humidity", `${(humidityValue / 100) * 360}deg`);
    }
    if (outdoorClimateCard) {
      outdoorClimateCard.style.setProperty("--climate-light", `${(lightValue / 1000) * 100}%`);
      outdoorClimateCard.style.setProperty("--outdoor-temp", `${((outdoorTempValue + 10) / 70) * 360}deg`);
    }
    if (climateGauge) {
      if (Number.isFinite(status.humidity)) {
        climateGauge.setAttribute("aria-valuenow", `${Math.round(humidityValue)}`);
        climateGauge.setAttribute("aria-valuetext", `${Math.round(humidityValue)} percent relative humidity`);
      } else {
        climateGauge.removeAttribute("aria-valuenow");
        climateGauge.setAttribute("aria-valuetext", "Humidity unavailable");
      }
    }
    if (outdoorTempGauge) {
      if (Number.isFinite(status.ds18b20Temperature)) {
        outdoorTempGauge.setAttribute("aria-valuenow", `${status.ds18b20Temperature.toFixed(1)}`);
        outdoorTempGauge.setAttribute("aria-valuetext", `${status.ds18b20Temperature.toFixed(1)} degrees Celsius`);
      } else {
        outdoorTempGauge.removeAttribute("aria-valuenow");
        outdoorTempGauge.setAttribute("aria-valuetext", "Outdoor temperature unavailable");
      }
    }
    if (lightGauge) {
      if (Number.isFinite(status.lux)) {
        lightGauge.setAttribute("aria-valuenow", `${Math.round(lightValue)}`);
        lightGauge.setAttribute("aria-valuetext", `${status.lux.toFixed(1)} lux`);
      } else {
        lightGauge.removeAttribute("aria-valuenow");
        lightGauge.setAttribute("aria-valuetext", "Outdoor light unavailable");
      }
    }
    setText("storageSettingsState", status.storageOnline ? "Online" : "Offline");
    setText("storageSettingsAvailable", formatBytes(status.storageAvailableBytes));
    setText("storageSettingsTotal", formatBytes(status.storageTotalBytes));
    setText("storageSettingsJedec", formatHex24(status.storageJedecId));
    setText("webStorageSettingsState", status.webStorageReady ? `${status.webStorageFileCount || 0} files` : "Not seeded");
    setText("webStorageSettingsEvent", status.webStorageLastEvent || "--");
    updateMp3Status(status);
    updateRadarStatus(status);
    updateTdsStatus(status);
    updateAirPurifierStatus(status);
    updateCommunicationStatus(status);
    updateInverterStatus(status);
    updateFm225Status(status);
    updateRfidStatus(status);
    updateAutomationStatus(status);
    setText("deviceName", status.deviceName);
    setText("ipAddress", status.ip);
    setText("wifiRssi", `${status.rssi} dBm`);
    setText("freeHeap", `${Math.round(status.freeHeap / 1024)} KB`);
    setText("uptime", formatUptime(status.uptimeSeconds));
    updateEsp32Status(status);
  } catch (error) {
    updateConnection("offline", "API offline");
  } finally {
    statusRefreshInFlight = false;
    if (pendingStatusRefresh) {
      pendingStatusRefresh = false;
      window.setTimeout(refreshStatus, 150);
    }
  }
}

async function runRfidAction(action, tag = "") {
  if (action === "clear" && !window.confirm("Clear all RFID tags?")) {
    return;
  }

  const payload = new URLSearchParams();
  if (tag) {
    payload.set("tag", tag);
  }

  const endpoints = {
    "start-add": "add-mode/start",
    "cancel-add": "add-mode/cancel",
    "save-pending": "add-mode/save"
  };
  const endpoint = endpoints[action] || action;

  rfidButtons.forEach((button) => {
    button.disabled = true;
  });

  try {
    const response = await fetch(`/api/rfid/${endpoint}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: payload,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`RFID action failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    setText("rfidActionState", "Command failed");
  } finally {
    rfidButtons.forEach((button) => {
      button.disabled = false;
    });
  }
}

function addFm225CommonEnrollPayload(payload) {
  payload.set("name", document.querySelector("#fm225EnrollName").value || "User");
  payload.set("direction", document.querySelector("#fm225EnrollDirection").value);
  payload.set("timeoutSec", document.querySelector("#fm225EnrollTimeout").value);
  payload.set("admin", document.querySelector("#fm225EnrollAdmin").checked ? "true" : "false");
}

function fm225PayloadFor(action, userId = null) {
  const payload = new URLSearchParams();

  if (action === "verify") {
    payload.set("timeoutSec", document.querySelector("#fm225VerifyTimeout").value);
  } else if (action === "get-user" || action === "delete-user") {
    payload.set("userId", userId === null ? "0" : `${userId}`);
  } else if (action.startsWith("enroll-")) {
    addFm225CommonEnrollPayload(payload);
    if (action === "enroll-integrated") {
      payload.set("enrollType", "0");
      payload.set("duplicateMode", "1");
    }
  } else if (action === "demo-on" || action === "demo-off") {
    payload.set("enabled", action === "demo-on" ? "true" : "false");
  }

  return payload;
}

function fm225EndpointFor(action) {
  const endpoints = {
    "demo-on": "demo",
    "demo-off": "demo"
  };
  return endpoints[action] || action;
}

async function runFm225Action(action, userId = null) {
  fm225Buttons.forEach((button) => {
    button.disabled = true;
  });
  if (action === "verify") {
    setText("fm225Recognized", "--");
    setText("fm225VerifyStatus", "Verifying face...");
    setText("fm225FaceVerifyResult", "Verifying face...");
    setText("fm225LastEvent", "Verifying face...");
  }

  try {
    const response = await fetch(`/api/fm225/${fm225EndpointFor(action)}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: fm225PayloadFor(action, userId),
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`FM225 action failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    setText("fm225LastEvent", "Command failed");
    setText("fm225DashboardStatus", "FM225 command failed");
  } finally {
    fm225Buttons.forEach((button) => {
      button.disabled = false;
    });
  }
}

async function runMp3Action(action) {
  mp3Buttons.forEach((button) => {
    button.disabled = true;
  });

  try {
    const body = new URLSearchParams();
    if (action === "file") {
      const trackInput = document.querySelector("#mp3TrackInput");
      body.set("track", trackInput ? trackInput.value : "1");
    }
    const response = await fetch(`/api/mp3/${action}`, {
      method: "POST",
      headers: action === "file" ? { "Content-Type": "application/x-www-form-urlencoded" } : undefined,
      body: action === "file" ? body : undefined,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`MP3 action failed: ${response.status}`);
    }

    updateMp3Status(await response.json());
    await refreshStatus();
  } catch (error) {
    setText("mp3PlayerState", "Command failed");
  } finally {
    mp3Buttons.forEach((button) => {
      button.disabled = false;
    });
  }
}

async function setMp3LiveVolume(volume) {
  setText("mp3LiveVolumeValue", volume);
  setValue("mp3VolumeInput", volume);

  const payload = new URLSearchParams();
  payload.set("volume", volume);

  try {
    const response = await fetch("/api/mp3/volume", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: payload,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`MP3 volume failed: ${response.status}`);
    }

    updateMp3Status(await response.json());
  } catch (error) {
    setText("mp3PlayerState", "Volume failed");
  }
}

function applySettings(settings) {
  if (Array.isArray(settings.relayConfigs)) {
    latestRelayConfigs = settings.relayConfigs;
  }
  renderRelaySettings(settings.relayConfigs);
  setChecked("rfidDoorUnlockEnabledInput", settings.rfidDoorUnlockEnabled);
  setValue("mp3VolumeInput", settings.mp3Volume ?? 25);
  setValue("mp3LiveVolumeInput", settings.mp3Volume ?? 25);
  setText("mp3LiveVolumeValue", settings.mp3Volume ?? 25);
  setChecked("mp3StartupSoundEnabledInput", settings.mp3StartupSoundEnabled);
  setValue("mp3StartupTrackInput", settings.mp3StartupTrack || 1);
  setChecked("mp3SmokeAlarmEnabledInput", settings.mp3SmokeAlarmEnabled);
  setValue("mp3SmokeAlarmTrackInput", settings.mp3SmokeAlarmTrack || 2);
  setValue("mp3SmokeAlarmThresholdRawInput", settings.mp3SmokeAlarmThresholdRaw ?? 2000);
  setChecked("tdsMonitorEnabledInput", settings.tdsMonitorEnabled);
  setValue("tdsMonitorAddressInput", settings.tdsMonitorAddress || "http://tds.local/api/tds");
  setChecked("airPurifierEnabledInput", settings.airPurifierEnabled);
  setValue("airPurifierAddressInput", settings.airPurifierAddress || "http://air-purifier.local/api/status");
  setChecked("rs485EnabledInput", settings.rs485Enabled);
  setValue("rs485BaudRateInput", settings.rs485BaudRate || 9600);
  setText("rs485RxPinText", `GPIO${settings.rs485RxPin ?? 15}`);
  setText("rs485TxPinText", `GPIO${settings.rs485TxPin ?? 18}`);
  setText("rs485DirectionPinText", `GPIO${settings.rs485DirectionPin ?? 48}`);
  setChecked("canEnabledInput", settings.canEnabled);
  setValue("canBitrateInput", settings.canBitrate || 500000);
  setText("canTxPinText", `GPIO${settings.canTxPin ?? 5}`);
  setText("canRxPinText", `GPIO${settings.canRxPin ?? 6}`);
  setChecked("solaxEnabledInput", settings.solaxEnabled);
  setValue("solaxAddressInput", settings.solaxAddress || "http://solax.local/");
  setValue("solaxPasswordInput", settings.solaxPassword || "");
  setValue("solaxIntervalSecondsInput", Math.round((settings.solaxIntervalMs || 10000) / 1000));
  setChecked("nitroxEnabledInput", settings.nitroxEnabled);
  setValue("nitroxHostInput", settings.nitroxHost || "nitrox.local");
  setValue("nitroxPortInput", settings.nitroxPort || 8899);
  setValue("nitroxLoggerSerialInput", settings.nitroxLoggerSerial || 1732083940);
  setValue("nitroxSlaveIdInput", settings.nitroxSlaveId || 1);
  setValue("nitroxIntervalSecondsInput", Math.round((settings.nitroxIntervalMs || 10000) / 1000));
  setChecked("growattEnabledInput", settings.growattEnabled);
  setValue("growattBaseUrlInput", settings.growattBaseUrl || "https://openapi.growatt.com/v1/");
  setValue("growattTokenInput", settings.growattToken || "");
  setValue("growattPlantIdInput", settings.growattPlantId || 0);
  setValue("growattIntervalSecondsInput", Math.round((settings.growattIntervalMs || 300000) / 1000));
  setValue("wifiSsidInput", settings.wifiSsid || "");
  setValue("wifiPasswordInput", "");
  setValue("mdnsHostnameInput", settings.mdnsHostname || "home-automation");
  setChecked("otaEnabledInput", settings.otaEnabled);
  setChecked("loginAuthEnabledInput", settings.loginAuthEnabled);
  setValue("loginUsernameInput", settings.loginUsername || "user");
  setValue("loginPasswordInput", "");
  setChecked("logRfidEnabledInput", settings.logRfidEnabled);
  setChecked("logFm225EnabledInput", settings.logFm225Enabled);
  setChecked("logDoorReedEnabledInput", settings.logDoorReedEnabled);
  setChecked("logGarageReedEnabledInput", settings.logGarageReedEnabled);
  setChecked("logDoorUnlockEnabledInput", settings.logDoorUnlockEnabled);
  setChecked("logGarageUnlockEnabledInput", settings.logGarageUnlockEnabled);
  setChecked("fm225RadarPresenceEnabledInput", settings.fm225RadarPresenceEnabled);
  setValue("fm225RadarMinDistanceCmInput", settings.fm225RadarMinDistanceCm);
  setValue("fm225RadarMinEnergyInput", settings.fm225RadarMinEnergy);
}

function relayPinLabel(config, index) {
  return config?.mcpPin || `${index < 8 ? "GPA" : "GPB"}${index % 8}`;
}

function renderRelaySettings(configs) {
  if (!relaySettingsGrid) {
    return;
  }

  const relayConfigs = Array.isArray(configs) ? configs : [];
  relaySettingsGrid.textContent = "";

  for (let index = 0; index < 16; index += 1) {
    const config = relayConfigs[index] || {};
    const relayNumber = index + 1;
    const card = document.createElement("article");
    card.className = "relay-settings-card";

    const title = document.createElement("div");
    title.className = "relay-settings-title";

    const heading = document.createElement("strong");
    heading.textContent = `Relay ${relayNumber}`;
    const pin = document.createElement("span");
    pin.textContent = relayPinLabel(config, index);
    title.append(heading, pin);

    const nameLabel = document.createElement("label");
    nameLabel.textContent = "Name";
    const nameInput = document.createElement("input");
    nameInput.name = `relay${relayNumber}Name`;
    nameInput.type = "text";
    nameInput.maxLength = 31;
    nameInput.value = config.name || `Relay ${relayNumber}`;
    nameLabel.append(nameInput);

    const modeLabel = document.createElement("label");
    modeLabel.textContent = "Mode";
    const modeSelect = document.createElement("select");
    modeSelect.name = `relay${relayNumber}Mode`;
    [
      ["manual", "Manual"],
      ["pulse", "Pulse"],
      ["automatic", "Automatic"]
    ].forEach(([value, label]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      modeSelect.append(option);
    });
    modeSelect.value = config.mode || "manual";
    modeLabel.append(modeSelect);

    const pulseLabel = document.createElement("label");
    pulseLabel.textContent = "Pulse Duration";
    const pulseInput = document.createElement("input");
    pulseInput.name = `relay${relayNumber}PulseDurationMs`;
    pulseInput.type = "number";
    pulseInput.min = "100";
    pulseInput.max = "604800000";
    pulseInput.step = "100";
    pulseInput.value = config.pulseDurationMs ?? 1000;
    pulseLabel.append(pulseInput);

    const pulseRoleLabelElement = document.createElement("label");
    pulseRoleLabelElement.textContent = "Pulse relay assignment";
    const pulseRoleSelect = document.createElement("select");
    pulseRoleSelect.name = `relay${relayNumber}PulseRole`;
    const roleUsedByOtherRelay = (role) =>
      relayConfigs.some((otherConfig, otherIndex) => otherIndex !== index && otherConfig?.mode === "pulse" && otherConfig?.pulseRole === role);
    [
      ["none", "None"],
      ["garageDoor", "Garage Door"],
      ["garageGate", "Garage Gate"]
    ].forEach(([value, label]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      option.disabled = value !== "none" && config.pulseRole !== value && roleUsedByOtherRelay(value);
      pulseRoleSelect.append(option);
    });
    pulseRoleSelect.value = config.pulseRole || "none";
    pulseRoleLabelElement.append(pulseRoleSelect);

    const automaticTypeLabel = document.createElement("label");
    automaticTypeLabel.textContent = "Automatic Control Type";
    const automaticTypeSelect = document.createElement("select");
    automaticTypeSelect.name = `relay${relayNumber}AutomaticControlType`;
    [["parametric", "Parametric"], ["schedule", "Schedule"]].forEach(([value, label]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      automaticTypeSelect.append(option);
    });
    automaticTypeSelect.value = config.automaticControlType || "parametric";
    automaticTypeLabel.append(automaticTypeSelect);

    const sensorLabelElement = document.createElement("label");
    sensorLabelElement.textContent = "Sensor";
    const sensorSelect = document.createElement("select");
    sensorSelect.name = `relay${relayNumber}Sensor`;
    [
      ["indoorTemperature", "Inside temperature"],
      ["outdoorTemperature", "Outside temperature DS18B20"],
      ["humidity", "Humidity"],
      ["lux", "Lux"],
      ["mq135", "MQ135 gas value"],
      ["pir", "PIR HC-SR501 motion state"]
    ].forEach(([value, label]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      sensorSelect.append(option);
    });
    sensorSelect.value = config.sensor || "indoorTemperature";
    sensorLabelElement.append(sensorSelect);

    const comparisonFieldLabel = document.createElement("label");
    comparisonFieldLabel.textContent = "Comparison condition";
    const comparisonSelect = document.createElement("select");
    comparisonSelect.name = `relay${relayNumber}Comparison`;
    [
      ["greaterThan", "Greater than"],
      ["lessThan", "Less than"],
      ["motionDetected", "Motion detected"],
      ["noMotionDetected", "No motion detected"]
    ].forEach(([value, label]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      comparisonSelect.append(option);
    });
    comparisonSelect.value = config.comparison || "greaterThan";
    comparisonFieldLabel.append(comparisonSelect);

    const onThresholdLabel = document.createElement("label");
    onThresholdLabel.textContent = "ON threshold";
    const onThresholdInput = document.createElement("input");
    onThresholdInput.name = `relay${relayNumber}OnThreshold`;
    onThresholdInput.type = "number";
    onThresholdInput.step = "0.1";
    onThresholdInput.value = config.onThreshold ?? 30;
    onThresholdLabel.append(onThresholdInput);

    const offThresholdLabel = document.createElement("label");
    offThresholdLabel.textContent = "OFF threshold";
    const offThresholdInput = document.createElement("input");
    offThresholdInput.name = `relay${relayNumber}OffThreshold`;
    offThresholdInput.type = "number";
    offThresholdInput.step = "0.1";
    offThresholdInput.value = config.offThreshold ?? 28;
    offThresholdLabel.append(offThresholdInput);

    const scheduleOnLabel = document.createElement("label");
    scheduleOnLabel.textContent = "ON time";
    const scheduleOnInput = document.createElement("input");
    scheduleOnInput.name = `relay${relayNumber}ScheduleOnTime`;
    scheduleOnInput.type = "time";
    scheduleOnInput.value = minutesToTime(config.scheduleOnMinutes ?? 480);
    scheduleOnLabel.append(scheduleOnInput);

    const scheduleOffLabel = document.createElement("label");
    scheduleOffLabel.textContent = "OFF time";
    const scheduleOffInput = document.createElement("input");
    scheduleOffInput.name = `relay${relayNumber}ScheduleOffTime`;
    scheduleOffInput.type = "time";
    scheduleOffInput.value = minutesToTime(config.scheduleOffMinutes ?? 1020);
    scheduleOffLabel.append(scheduleOffInput);

    const weekdaysLabel = document.createElement("label");
    weekdaysLabel.textContent = "Enabled days of the week";
    const weekdaysInput = document.createElement("input");
    weekdaysInput.name = `relay${relayNumber}EnabledWeekdays`;
    weekdaysInput.type = "hidden";
    weekdaysInput.value = config.enabledWeekdays ?? 127;
    const weekdayRow = document.createElement("div");
    weekdayRow.className = "weekday-toggle-row";
    ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"].forEach((day, dayIndex) => {
      const dayLabel = document.createElement("label");
      dayLabel.className = "weekday-toggle";
      const dayInput = document.createElement("input");
      dayInput.type = "checkbox";
      dayInput.name = `relay${relayNumber}Weekday${dayIndex}`;
      dayInput.checked = ((config.enabledWeekdays ?? 127) & (1 << dayIndex)) !== 0;
      const dayText = document.createElement("span");
      dayText.textContent = day;
      dayLabel.append(dayInput, dayText);
      weekdayRow.append(dayLabel);
    });
    weekdaysLabel.append(weekdaysInput, weekdayRow);

    const overviewLabel = document.createElement("label");
    overviewLabel.className = "relay-dashboard-toggle";
    const overviewInput = document.createElement("input");
    overviewInput.name = `relay${relayNumber}ShowInDashboard`;
    overviewInput.type = "checkbox";
    overviewInput.checked = Boolean(config.showInDashboard);
    const overviewText = document.createElement("span");
    overviewText.textContent = "Show in Dashboard";
    overviewLabel.append(overviewInput, overviewText);

    const saveButton = document.createElement("button");
    saveButton.className = "icon-action relay-save-button";
    saveButton.type = "button";
    saveButton.textContent = "Save";
    saveButton.addEventListener("click", () => saveRelayCard(relayNumber));

    const setFieldGroupVisible = (label, visible) => {
      label.classList.toggle("hidden", !visible);
      label.querySelectorAll("input, select, textarea").forEach((control) => {
        control.disabled = !visible;
      });
    };

    function updateVisibleFields() {
      const mode = modeSelect.value;
      const automaticType = automaticTypeSelect.value;
      const isPir = sensorSelect.value === "pir";
      if (mode !== "pulse") {
        pulseRoleSelect.value = "none";
      }
      setFieldGroupVisible(pulseLabel, mode === "pulse");
      setFieldGroupVisible(pulseRoleLabelElement, mode === "pulse");
      setFieldGroupVisible(automaticTypeLabel, mode === "automatic");
      setFieldGroupVisible(sensorLabelElement, mode === "automatic" && automaticType === "parametric");
      setFieldGroupVisible(comparisonFieldLabel, mode === "automatic" && automaticType === "parametric");
      setFieldGroupVisible(onThresholdLabel, mode === "automatic" && automaticType === "parametric" && !isPir);
      setFieldGroupVisible(offThresholdLabel, mode === "automatic" && automaticType === "parametric" && !isPir);
      setFieldGroupVisible(scheduleOnLabel, mode === "automatic" && automaticType === "schedule");
      setFieldGroupVisible(scheduleOffLabel, mode === "automatic" && automaticType === "schedule");
      setFieldGroupVisible(weekdaysLabel, mode === "automatic" && automaticType === "schedule");
    }

    modeSelect.addEventListener("change", updateVisibleFields);
    modeSelect.addEventListener("change", updatePulseRoleOptions);
    pulseRoleSelect.addEventListener("change", updatePulseRoleOptions);
    automaticTypeSelect.addEventListener("change", updateVisibleFields);
    sensorSelect.addEventListener("change", updateVisibleFields);

    card.append(
      title,
      nameLabel,
      modeLabel,
      pulseLabel,
      pulseRoleLabelElement,
      automaticTypeLabel,
      sensorLabelElement,
      comparisonFieldLabel,
      onThresholdLabel,
      offThresholdLabel,
      scheduleOnLabel,
      scheduleOffLabel,
      weekdaysLabel,
      overviewLabel,
      saveButton
    );
    updateVisibleFields();
    relaySettingsGrid.append(card);
  }
  updatePulseRoleOptions();
}

async function saveRelayCard(relayNumber) {
  settingsMessage.textContent = "Saving relay...";
  try {
    const response = await fetch("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: formToPayload(settingsForm),
      cache: "no-store"
    });
    if (!response.ok) {
      throw new Error(`Save failed: ${response.status}`);
    }
    applySettings(await response.json());
    settingsMessage.textContent = `Relay ${relayNumber} saved`;
    await refreshStatus();
  } catch (error) {
    settingsMessage.textContent = `Relay ${relayNumber} save failed`;
  }
}

async function loadSettings() {
  const response = await fetchWithTimeout("/api/settings", { cache: "no-store" }, 1800);
  if (!response.ok) {
    throw new Error(`Settings API failed: ${response.status}`);
  }
  applySettings(await response.json());
}

function formToPayload(form) {
  const payload = new URLSearchParams();
  payload.set("rfidDoorUnlockEnabled", form.rfidDoorUnlockEnabled.checked ? "true" : "false");
  payload.set("mp3Volume", form.mp3Volume.value);
  payload.set("mp3StartupSoundEnabled", form.mp3StartupSoundEnabled.checked ? "true" : "false");
  payload.set("mp3StartupTrack", form.mp3StartupTrack.value);
  payload.set("mp3SmokeAlarmEnabled", form.mp3SmokeAlarmEnabled.checked ? "true" : "false");
  payload.set("mp3SmokeAlarmTrack", form.mp3SmokeAlarmTrack.value);
  payload.set("mp3SmokeAlarmThresholdRaw", form.mp3SmokeAlarmThresholdRaw.value);
  payload.set("tdsMonitorEnabled", form.tdsMonitorEnabled.checked ? "true" : "false");
  payload.set("tdsMonitorAddress", form.tdsMonitorAddress.value);
  payload.set("airPurifierEnabled", form.airPurifierEnabled.checked ? "true" : "false");
  payload.set("airPurifierAddress", form.airPurifierAddress.value);
  payload.set("rs485Enabled", form.rs485Enabled.checked ? "true" : "false");
  payload.set("rs485BaudRate", form.rs485BaudRate.value);
  payload.set("canEnabled", form.canEnabled.checked ? "true" : "false");
  payload.set("canBitrate", form.canBitrate.value);
  payload.set("solaxEnabled", form.solaxEnabled.checked ? "true" : "false");
  payload.set("solaxAddress", form.solaxAddress.value);
  payload.set("solaxPassword", form.solaxPassword.value);
  payload.set("solaxIntervalSeconds", form.solaxIntervalSeconds.value);
  payload.set("nitroxEnabled", form.nitroxEnabled.checked ? "true" : "false");
  payload.set("nitroxHost", form.nitroxHost.value);
  payload.set("nitroxPort", form.nitroxPort.value);
  payload.set("nitroxLoggerSerial", form.nitroxLoggerSerial.value);
  payload.set("nitroxSlaveId", form.nitroxSlaveId.value);
  payload.set("nitroxIntervalSeconds", form.nitroxIntervalSeconds.value);
  payload.set("growattEnabled", form.growattEnabled.checked ? "true" : "false");
  payload.set("growattBaseUrl", form.growattBaseUrl.value);
  payload.set("growattToken", form.growattToken.value);
  payload.set("growattPlantId", form.growattPlantId.value);
  payload.set("growattIntervalSeconds", form.growattIntervalSeconds.value);
  payload.set("wifiSsid", form.wifiSsid.value);
  payload.set("wifiPassword", form.wifiPassword.value);
  payload.set("mdnsHostname", form.mdnsHostname.value);
  payload.set("otaEnabled", form.otaEnabled.checked ? "true" : "false");
  payload.set("loginAuthEnabled", form.loginAuthEnabled.checked ? "true" : "false");
  payload.set("loginUsername", form.loginUsername.value);
  payload.set("loginPassword", form.loginPassword.value);
  payload.set("logRfidEnabled", form.logRfidEnabled.checked ? "true" : "false");
  payload.set("logFm225Enabled", form.logFm225Enabled.checked ? "true" : "false");
  payload.set("logDoorReedEnabled", form.logDoorReedEnabled.checked ? "true" : "false");
  payload.set("logGarageReedEnabled", form.logGarageReedEnabled.checked ? "true" : "false");
  payload.set("logDoorUnlockEnabled", form.logDoorUnlockEnabled.checked ? "true" : "false");
  payload.set("logGarageUnlockEnabled", form.logGarageUnlockEnabled.checked ? "true" : "false");
  payload.set("fm225RadarPresenceEnabled", form.fm225RadarPresenceEnabled.checked ? "true" : "false");
  payload.set("fm225RadarMinDistanceCm", form.fm225RadarMinDistanceCm.value);
  payload.set("fm225RadarMinEnergy", form.fm225RadarMinEnergy.value);
  for (let index = 1; index <= 16; index += 1) {
    const config = latestRelayConfigs[index - 1] || {};
    payload.set(`relay${index}Name`, form[`relay${index}Name`]?.value || `Relay ${index}`);
    payload.set(`relay${index}Mode`, form[`relay${index}Mode`]?.value || "manual");
    payload.set(`relay${index}PulseDurationMs`, form[`relay${index}PulseDurationMs`]?.value || String(config.pulseDurationMs || 1000));
    payload.set(`relay${index}PulseRole`, form[`relay${index}PulseRole`]?.value || config.pulseRole || "none");
    payload.set(`relay${index}ShowInDashboard`, form[`relay${index}ShowInDashboard`]?.checked ? "true" : "false");
    payload.set(`relay${index}CurrentState`, config.currentState ? "true" : "false");
    payload.set(`relay${index}AutomaticControlType`, form[`relay${index}AutomaticControlType`]?.value || config.automaticControlType || "parametric");
    payload.set(`relay${index}Sensor`, form[`relay${index}Sensor`]?.value || config.sensor || "indoorTemperature");
    payload.set(`relay${index}Comparison`, form[`relay${index}Comparison`]?.value || config.comparison || "greaterThan");
    payload.set(`relay${index}OnThreshold`, form[`relay${index}OnThreshold`]?.value || String(config.onThreshold ?? 30));
    payload.set(`relay${index}OffThreshold`, form[`relay${index}OffThreshold`]?.value || String(config.offThreshold ?? 28));
    payload.set(
      `relay${index}ScheduleOnMinutes`,
      form[`relay${index}ScheduleOnTime`] ? String(timeToMinutes(form[`relay${index}ScheduleOnTime`].value)) : String(config.scheduleOnMinutes ?? 480)
    );
    payload.set(
      `relay${index}ScheduleOffMinutes`,
      form[`relay${index}ScheduleOffTime`] ? String(timeToMinutes(form[`relay${index}ScheduleOffTime`].value)) : String(config.scheduleOffMinutes ?? 1020)
    );
    const weekdayControls = Array.from({ length: 7 }, (_, day) => form[`relay${index}Weekday${day}`]).filter(Boolean);
    let weekdayMask = weekdayControls.length > 0 ? 0 : Number(config.enabledWeekdays ?? 127);
    for (let day = 0; day < 7; day += 1) {
      if (form[`relay${index}Weekday${day}`]?.checked) {
        weekdayMask |= 1 << day;
      }
    }
    payload.set(`relay${index}EnabledWeekdays`, String(weekdayMask));
  }
  return payload;
}

function renderLogs(logs) {
  const list = document.querySelector("#logsList");
  if (!list) {
    return;
  }

  list.textContent = "";
  if (!Array.isArray(logs) || logs.length === 0) {
    list.textContent = "No logs saved.";
    return;
  }

  logs.slice().reverse().forEach((log) => {
    const row = document.createElement("div");
    row.className = "log-entry";

    const time = document.createElement("span");
    time.textContent = formatUptime(Number(log.uptimeSeconds) || 0);

    const category = document.createElement("strong");
    category.textContent = String(log.category || "event").replaceAll("_", " ");

    const message = document.createElement("div");
    message.textContent = log.message || "";

    row.append(time, category, message);
    list.append(row);
  });
}

async function loadEventLogs() {
  const list = document.querySelector("#logsList");
  if (list) {
    list.textContent = "Loading logs...";
  }

  const response = await fetchWithTimeout("/api/logs", { cache: "no-store" }, 1800);
  if (!response.ok) {
    throw new Error(`Logs API failed: ${response.status}`);
  }

  const payload = await response.json();
  renderLogs(payload.logs);
}

tabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    const selectedTab = tab.dataset.tab;

    tabs.forEach((item) => item.classList.toggle("active", item === tab));
    Object.entries(panels).forEach(([name, panel]) => {
      panel.classList.toggle("active", name === selectedTab);
    });
  });
});

settingsSubtabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    const selectedTab = tab.dataset.settingsTab;

    settingsSubtabs.forEach((item) => item.classList.toggle("active", item === tab));
    settingsSubpanels.forEach((panel) => {
      panel.classList.toggle("active", panel.dataset.settingsPanel === selectedTab);
    });
  });
});

if (restartDeviceButton) {
  restartDeviceButton.addEventListener("click", async () => {
    if (!window.confirm("Restart controller now?")) {
      return;
    }

    restartDeviceButton.disabled = true;
    settingsMessage.textContent = "Restarting...";

    try {
      await fetch("/api/restart", {
        method: "POST",
        cache: "no-store"
      });
    } catch (error) {
      // The controller may drop the connection before the response completes.
    }
  });
}

if (loadLogsButton) {
  loadLogsButton.addEventListener("click", async () => {
    loadLogsButton.disabled = true;
    try {
      await loadEventLogs();
    } catch (error) {
      const list = document.querySelector("#logsList");
      if (list) {
        list.textContent = "Failed to load logs.";
      }
    } finally {
      loadLogsButton.disabled = false;
    }
  });
}

settingsForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  settingsMessage.textContent = "Saving...";

  try {
    const response = await fetch("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: formToPayload(settingsForm)
    });

    if (!response.ok) {
      throw new Error(`Save failed: ${response.status}`);
    }

    const savedSettings = await response.json();
    applySettings(savedSettings);
    settingsMessage.textContent = savedSettings.inverterSettingsSaved === false ? "Saved, inverter storage failed" : "Saved";
    await refreshStatus();
  } catch (error) {
    settingsMessage.textContent = "Save failed";
  }
});

mp3Buttons.forEach((button) => {
  button.addEventListener("click", () => {
    runMp3Action(button.dataset.action);
  });
});

const mp3LiveVolumeInput = document.querySelector("#mp3LiveVolumeInput");
if (mp3LiveVolumeInput) {
  mp3LiveVolumeInput.addEventListener("input", () => {
    setMp3LiveVolume(mp3LiveVolumeInput.value);
  });
}

if (rs485TestSendButton) {
  rs485TestSendButton.addEventListener("click", async () => {
    rs485TestSendButton.disabled = true;
    settingsMessage.textContent = "Sending RS485 test...";
    try {
      const payload = new URLSearchParams();
      payload.set("text", document.querySelector("#rs485TestTextInput")?.value || "RS485 demo");
      const response = await fetch("/api/rs485/test-send", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: payload,
        cache: "no-store"
      });
      settingsMessage.textContent = response.ok ? "RS485 test sent" : "RS485 test failed";
      await refreshStatus();
    } catch (error) {
      settingsMessage.textContent = "RS485 test failed";
    } finally {
      rs485TestSendButton.disabled = false;
    }
  });
}

if (canTestSendButton) {
  canTestSendButton.addEventListener("click", async () => {
    canTestSendButton.disabled = true;
    settingsMessage.textContent = "Sending CAN test...";
    try {
      const payload = new URLSearchParams();
      payload.set("id", document.querySelector("#canTestIdInput")?.value || "291");
      payload.set("length", document.querySelector("#canTestLengthInput")?.value || "0");
      payload.set("byte0", document.querySelector("#canTestByte0Input")?.value || "0");
      payload.set("byte1", document.querySelector("#canTestByte1Input")?.value || "0");
      const response = await fetch("/api/can/test-send", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: payload,
        cache: "no-store"
      });
      settingsMessage.textContent = response.ok ? "CAN test sent" : "CAN test failed";
      await refreshStatus();
    } catch (error) {
      settingsMessage.textContent = "CAN test failed";
    } finally {
      canTestSendButton.disabled = false;
    }
  });
}

fm225Buttons.forEach((button) => {
  button.addEventListener("click", () => {
    runFm225Action(button.dataset.action);
  });
});

rfidButtons.forEach((button) => {
  button.addEventListener("click", () => {
    runRfidAction(button.dataset.action);
  });
});

refreshStatus();
loadSettings().catch(() => {
  settingsMessage.textContent = "Load failed";
});
setInterval(refreshStatus, 2000);
