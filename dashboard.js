const MQTT_URL = "wss://broker.hivemq.com:8884/mqtt";
const MQTT_BASE = "smartfarm/rsd2s3g3";

const TOPICS = {
    soil: `${MQTT_BASE}/module1/soil`,
    soilPercent: `${MQTT_BASE}/module1/soil_percent`,
    pump: `${MQTT_BASE}/module1/pump`,
    pumpMode: `${MQTT_BASE}/module1/pump/mode`,
    irrigationRule: `${MQTT_BASE}/module1/irrigation_rule`,
    temperature: `${MQTT_BASE}/module2/temperature`,
    humidity: `${MQTT_BASE}/module2/humidity`,
    light: `${MQTT_BASE}/module2/light`,
    fan: `${MQTT_BASE}/module2/fan`,
    fanMode: `${MQTT_BASE}/module2/fan/mode`,
    led: `${MQTT_BASE}/module2/led`,
    ledMode: `${MQTT_BASE}/module2/led/mode`,
    water: `${MQTT_BASE}/module3/water`,
    waterPercent: `${MQTT_BASE}/module3/water_percent`,
    pumpRuntime: `${MQTT_BASE}/module3/pump_runtime_seconds`,
    estimatedWater: `${MQTT_BASE}/module3/estimated_water_litres`,
    lowWater: `${MQTT_BASE}/module3/low_water`,
    pumpLock: `${MQTT_BASE}/module3/pump_lock`,
    rainProbability: `${MQTT_BASE}/weather/rain_probability`,
    rainExpected: `${MQTT_BASE}/weather/rain_expected`,
    weatherStatus: `${MQTT_BASE}/weather/status`,
    systemAlert: `${MQTT_BASE}/system/alert`,
    nodeRedCloud: `${MQTT_BASE}/system/cloud`,
    nodeRedQueue: `${MQTT_BASE}/system/queue`,
    module4Status: `${MQTT_BASE}/module4/status`,
    module4Alert: `${MQTT_BASE}/module4/alert`,
    module4Online: `${MQTT_BASE}/module4/online`,
    pumpControl: `${MQTT_BASE}/control/pump`,
    pumpModeControl: `${MQTT_BASE}/control/pump/mode`,
    pumpAutoControl: `${MQTT_BASE}/control/pump/auto`,
    fanControl: `${MQTT_BASE}/control/fan`,
    fanModeControl: `${MQTT_BASE}/control/fan/mode`,
    ledControl: `${MQTT_BASE}/control/led`,
    ledModeControl: `${MQTT_BASE}/control/led/mode`,
    module4CaptureTrigger: `${MQTT_BASE}/control/module4/capture`,
    module4IntervalControl: `${MQTT_BASE}/config/module4/capture_interval_seconds`,
    module4IntervalStatus: `${MQTT_BASE}/module4/capture_interval_seconds`,
    configSoilStart: `${MQTT_BASE}/config/soil/pump_on_percent`,
    configSoilStop: `${MQTT_BASE}/config/soil/pump_off_percent`,
    configPumpPulse: `${MQTT_BASE}/config/irrigation/pump_pulse_seconds`,
    configFanOn: `${MQTT_BASE}/config/fan/on_temperature_c`,
    configFanOff: `${MQTT_BASE}/config/fan/off_temperature_c`,
    configLdrDark: `${MQTT_BASE}/config/light/dark_raw`,
    configLowWater: `${MQTT_BASE}/config/water/low_percent`,
    configRainDelay: `${MQTT_BASE}/config/irrigation/rain_probability`,
    configHumidityDelay: `${MQTT_BASE}/config/irrigation/humidity_percent`
};

const AUTOMATION_DEFAULTS = {
    soilStartSetting: 30,
    soilStopSetting: 45,
    pumpPulseSetting: 1,
    fanOnSetting: 30,
    fanOffSetting: 28,
    ldrDarkSetting: 1200,
    lowWaterSetting: 20,
    rainDelaySetting: 65,
    humidityDelaySetting: 85
};

const AUTOMATION_CONFIG = [
    { id: "soilStartSetting", topic: TOPICS.configSoilStart },
    { id: "soilStopSetting", topic: TOPICS.configSoilStop },
    { id: "pumpPulseSetting", topic: TOPICS.configPumpPulse },
    { id: "fanOnSetting", topic: TOPICS.configFanOn },
    { id: "fanOffSetting", topic: TOPICS.configFanOff },
    { id: "ldrDarkSetting", topic: TOPICS.configLdrDark },
    { id: "lowWaterSetting", topic: TOPICS.configLowWater },
    { id: "rainDelaySetting", topic: TOPICS.configRainDelay },
    { id: "humidityDelaySetting", topic: TOPICS.configHumidityDelay }
];

const AUTOMATION_CONFIG_BY_TOPIC = Object.fromEntries(
    AUTOMATION_CONFIG.map(setting => [setting.topic, setting])
);

const actuatorModes = { pump: "AUTO", fan: "AUTO", led: "AUTO" };
const actuatorStates = { pump: "OFF", fan: "OFF", led: "OFF" };
const mqttActivityEntries = [];
const mqttActivityLastByTopic = new Map();
let currentPumpLocked = false;
let currentSystemAlert = "";
let lastNodeRedHeartbeatAt = 0;
let module4ServiceOnline = false;
let client;

const byId = id => document.getElementById(id);

function appendMqttActivity(message) {
    const timestamp = new Date().toLocaleTimeString("en-MY");
    mqttActivityEntries.unshift(`[${timestamp}] ${message}`);
    if (mqttActivityEntries.length > 120) mqttActivityEntries.length = 120;
    const element = byId("mqttActivityLog");
    if (element) element.textContent = mqttActivityEntries.join("\n");
}

function clearMqttActivity() {
    mqttActivityEntries.length = 0;
    mqttActivityLastByTopic.clear();
    byId("mqttActivityLog").textContent = "Log cleared.";
}

function logIncomingMqtt(topic, value) {
    const now = Date.now();
    const suffix = topic.startsWith(`${MQTT_BASE}/`) ? topic.slice(MQTT_BASE.length + 1) : topic;
    const previous = mqttActivityLastByTopic.get(topic);
    const isSensor = /^(module1\/soil|module2\/(temperature|humidity|light)|module3\/water)/.test(suffix);

    if (previous) {
        if (isSensor && now - previous.time < 10000) return;
        if (!isSensor && previous.value === value && now - previous.time < 15000) return;
    }
    mqttActivityLastByTopic.set(topic, { value, time: now });

    const descriptions = {
        "control/pump/auto": `Node-RED AUTO pump decision: ${value}`,
        "module1/irrigation_rule": `Irrigation rule: ${value}`,
        "module1/pump": `Pump feedback: ${value}`,
        "module2/fan": `Fan feedback: ${value}`,
        "module2/led": `Grow-light feedback: ${value}`,
        "module3/pump_lock": `Pump safety lock: ${value}`,
        "weather/status": `Weather service status: ${value}`,
        "weather/rain_expected": `Rain expected: ${value}`,
        "system/alert": value ? `System alert: ${value}` : "System alert cleared"
    };
    appendMqttActivity(descriptions[suffix] || `IN  ${suffix} <= ${value}`);
}

function logOutgoingMqtt(topic, value) {
    const suffix = topic.startsWith(`${MQTT_BASE}/`) ? topic.slice(MQTT_BASE.length + 1) : topic;
    appendMqttActivity(`OUT ${suffix} => ${value}`);
}

function setConnectionState(text, connected = false) {
    const dot = byId("mqttPulse");
    const label = byId("mqttStatusText");
    if (dot) dot.classList.toggle("online", connected);
    if (label) label.textContent = text;
    if (!connected) setNodeRedStatus("Node-RED: Unknown", false);
}

function setNodeRedStatus(text, online) {
    const dot = byId("nodeRedPulse");
    const label = byId("nodeRedStatusText");
    if (dot) dot.classList.toggle("online", online);
    if (label) label.textContent = text;
}

function markNodeRedHeartbeat() {
    lastNodeRedHeartbeatAt = Date.now();
    setNodeRedStatus("Node-RED: Online", true);
}

function checkNodeRedHeartbeat() {
    if (!client?.connected) {
        setNodeRedStatus("Node-RED: Unknown", false);
    } else if (!lastNodeRedHeartbeatAt) {
        setNodeRedStatus("Node-RED: Checking...", false);
    } else if (Date.now() - lastNodeRedHeartbeatAt > 40000) {
        setNodeRedStatus("Node-RED: Offline", false);
    }
}

function setModule4ServiceStatus(online) {
    module4ServiceOnline = online;
    const badge = byId("module4ServiceStatus");
    if (!badge) return;
    badge.textContent = online ? "SERVICE ONLINE" : "SERVICE OFFLINE";
    badge.className = `status-pill ${online ? "pill-on" : "pill-lock"}`;
}

function forceMqttReconnect(userRequested = false) {
    if (!client) {
        setConnectionState("MQTT library unavailable", false);
        return;
    }
    if (client.connected) {
        if (userRequested) appendMqttActivity("MQTT is already connected.");
        return;
    }
    if (client.reconnecting) {
        setConnectionState("Reconnecting...", false);
        return;
    }
    setConnectionState("Reconnecting now...", false);
    try {
        client.reconnect();
    } catch (error) {
        appendMqttActivity(`Reconnect failed: ${error.message}`);
    }
}

function renderAlertRibbon() {
    const ribbon = byId("topEmergencyRibbon");
    const text = byId("topEmergencyText");
    if (currentPumpLocked) {
        const cutoff = byId("lowWaterSetting")?.value || "20";
        text.textContent = `SAFETY INTERLOCK: Tank level is below ${cutoff}%. Pump operation is blocked.`;
        ribbon.style.display = "flex";
        return;
    }
    if (currentSystemAlert) {
        text.textContent = currentSystemAlert;
        ribbon.style.display = "flex";
        return;
    }
    ribbon.style.display = "none";
}

function updateActuator(actuator, value) {
    const state = String(value).trim().toUpperCase();
    if (!['ON', 'OFF'].includes(state)) return;
    actuatorStates[actuator] = state;
    const pillId = actuator === "led" ? "ledPill" : `${actuator}Pill`;
    const pill = byId(pillId);
    const hiddenStatus = byId(`${actuator}Status`);
    const label = actuator === "led" ? "LIGHT" : actuator.toUpperCase();
    if (pill) {
        pill.textContent = `${label} ${state}`;
        pill.className = `status-pill ${state === "ON" ? "pill-on" : "pill-off"}`;
    }
    if (hiddenStatus) hiddenStatus.textContent = state;
    updateLcdSimulation();
}

function syncModeUi(actuator, mode) {
    const normalized = String(mode).trim().toUpperCase();
    if (!['AUTO', 'MANUAL'].includes(normalized)) return;
    actuatorModes[actuator] = normalized;
    const label = byId(`${actuator}Mode`);
    if (label) label.textContent = normalized;

    document.querySelectorAll(`.btn-mode[data-actuator="${actuator}"]`).forEach(button => {
        button.classList.toggle("active", button.dataset.mode === normalized);
        button.classList.remove("pending");
    });
    document.querySelectorAll(`.manual-control-button[data-actuator="${actuator}"]`).forEach(button => {
        button.disabled = normalized !== "MANUAL";
        button.title = normalized === "MANUAL"
            ? "Send a manual command"
            : "Switch to MANUAL before using ON/OFF";
    });
    updateLcdSimulation();
}

function setActuatorMode(actuator, mode) {
    if (!client?.connected) {
        alert("MQTT is not connected.");
        return;
    }
    const normalized = String(mode).toUpperCase();
    if (!['AUTO', 'MANUAL'].includes(normalized)) return;
    const previousMode = actuatorModes[actuator];
    const topic = TOPICS[`${actuator}ModeControl`];
    syncModeUi(actuator, normalized);

    document.querySelectorAll(`.btn-mode[data-actuator="${actuator}"]`).forEach(button => button.classList.add("pending"));
    client.publish(topic, normalized, { qos: 1, retain: true }, error => {
        document.querySelectorAll(`.btn-mode[data-actuator="${actuator}"]`).forEach(button => button.classList.remove("pending"));
        if (error) {
            syncModeUi(actuator, previousMode);
            alert(`Unable to switch ${actuator.toUpperCase()} mode. Please try again.`);
        }
    });
    logOutgoingMqtt(topic, normalized);
}

function publishManualCommand(actuator, topic, state) {
    if (!client?.connected) {
        alert("MQTT is not connected.");
        return;
    }
    if (actuatorModes[actuator] !== "MANUAL") {
        alert(`Switch ${actuator.toUpperCase()} to MANUAL before sending ON/OFF commands.`);
        return;
    }
    client.publish(topic, state, { qos: 0, retain: false });
    logOutgoingMqtt(topic, state);
}

function controlPump(state) { publishManualCommand("pump", TOPICS.pumpControl, state); }
function controlFan(state) { publishManualCommand("fan", TOPICS.fanControl, state); }
function controlLED(state) { publishManualCommand("led", TOPICS.ledControl, state); }

function setModule4CaptureInterval(seconds) {
    if (!client?.connected) {
        alert("MQTT is not connected.");
        return;
    }
    client.publish(TOPICS.module4IntervalControl, String(seconds), { qos: 1, retain: true });
    byId("cropControlStatus").textContent = `Automatic capture interval set to ${Math.round(seconds / 60)} min.`;
    logOutgoingMqtt(TOPICS.module4IntervalControl, seconds);
}

function triggerModule4CaptureNow() {
    if (!client?.connected) {
        alert("MQTT is not connected.");
        return;
    }
    if (!module4ServiceOnline) {
        byId("cropControlStatus").textContent = "Module 4 is offline. Start the Python service first.";
        return;
    }
    const button = byId("cropCaptureNowBtn");
    const previousCapture = window.latestCropCaptureTimestamp || "";
    button.disabled = true;
    button.textContent = "Capturing...";
    byId("cropControlStatus").textContent = "Capture requested; waiting for upload...";
    client.publish(TOPICS.module4CaptureTrigger, String(Date.now()), { qos: 0, retain: false });
    logOutgoingMqtt(TOPICS.module4CaptureTrigger, "NOW");

    let attempts = 0;
    const poll = window.setInterval(async () => {
        attempts += 1;
        if (typeof refreshCloudDashboard === "function") {
            try {
                await refreshCloudDashboard();
            } catch (error) {
                console.warn("Module 4 refresh failed:", error);
            }
        }
        const newCapture = window.latestCropCaptureTimestamp || "";
        if (newCapture && newCapture !== previousCapture) {
            window.clearInterval(poll);
            button.disabled = false;
            button.textContent = "📸 Capture Now";
            byId("cropControlStatus").textContent = "New image received from Module 4.";
        } else if (attempts >= 10) {
            window.clearInterval(poll);
            button.disabled = false;
            button.textContent = "📸 Capture Now";
            byId("cropControlStatus").textContent = "No new valid image. Check the Module 4 message below and the Python window.";
        }
    }, 3000);
}

function updateLcdSimulation() {
    const temperature = byId("temperature")?.textContent || "--";
    const humidity = byId("humidity")?.textContent || "--";
    byId("lcdLine1").textContent = `T:${temperature}C H:${humidity}%`.slice(0, 16).padEnd(16, " ");
    byId("lcdLine2").textContent = `Pump:${actuatorStates.pump} ${actuatorModes.pump}`.slice(0, 16).padEnd(16, " ");
}

function setAutomationSettingsStatus(message, style = "") {
    const element = byId("automationSettingsStatus");
    element.textContent = message;
    element.className = style;
}

function updateAutomationSummaries() {
    byId("soilStartSummary").textContent = `${byId("soilStartSetting").value}%`;
    byId("fanOnSummary").textContent = byId("fanOnSetting").value;
    byId("fanOffSummary").textContent = byId("fanOffSetting").value;
    byId("ldrSummary").textContent = byId("ldrDarkSetting").value;
    renderAlertRibbon();
}

function readAutomationSettings() {
    const values = {};
    for (const setting of AUTOMATION_CONFIG) {
        const input = byId(setting.id);
        const value = Number(input.value);
        const minimum = Number(input.min);
        const maximum = Number(input.max);
        if (!Number.isFinite(value) || value < minimum || value > maximum) {
            throw new Error(`${input.previousElementSibling.textContent} must be between ${minimum} and ${maximum}.`);
        }
        values[setting.id] = value;
    }
    if (values.soilStopSetting <= values.soilStartSetting) {
        throw new Error("Pump clear threshold must be higher than the pump start threshold.");
    }
    if (values.fanOnSetting <= values.fanOffSetting) {
        throw new Error("Fan ON temperature must be higher than the fan OFF temperature.");
    }
    return values;
}

function saveAutomationSettings() {
    if (!client?.connected) {
        setAutomationSettingsStatus("MQTT is not connected. Settings were not sent.", "error");
        return;
    }
    let values;
    try {
        values = readAutomationSettings();
    } catch (error) {
        setAutomationSettingsStatus(error.message, "error");
        return;
    }

    updateAutomationSummaries();
    setAutomationSettingsStatus("Applying settings...", "");
    let pending = AUTOMATION_CONFIG.length;
    let failed = false;
    for (const setting of AUTOMATION_CONFIG) {
        const value = values[setting.id];
        logOutgoingMqtt(setting.topic, value);
        client.publish(setting.topic, String(value), { qos: 1, retain: true }, error => {
            failed ||= Boolean(error);
            pending -= 1;
            if (pending === 0) {
                setAutomationSettingsStatus(
                    failed ? "Some settings could not be sent. Please try again." : "Settings saved. ESP32 and Node-RED will use the new thresholds.",
                    failed ? "error" : "success"
                );
            }
        });
    }
}

function restoreAutomationDefaults() {
    for (const [id, value] of Object.entries(AUTOMATION_DEFAULTS)) byId(id).value = value;
    updateAutomationSummaries();
    saveAutomationSettings();
}

function toggleAccordion(id) {
    const drawer = byId(id);
    const header = drawer.previousElementSibling;
    const willOpen = drawer.style.display !== "block";
    drawer.style.display = willOpen ? "block" : "none";
    byId("drawerArrow").textContent = willOpen ? "▲" : "▼";
    header.setAttribute("aria-expanded", String(willOpen));
}

function handleMqttMessage(topic, payload) {
    const value = payload.toString().trim();
    const upper = value.toUpperCase();
    logIncomingMqtt(topic, value);

    if (topic === TOPICS.nodeRedCloud || topic === TOPICS.nodeRedQueue) {
        markNodeRedHeartbeat();
    }

    const setting = AUTOMATION_CONFIG_BY_TOPIC[topic];
    if (setting) {
        const numericValue = Number(value);
        if (Number.isFinite(numericValue)) {
            byId(setting.id).value = numericValue;
            updateAutomationSummaries();
            setAutomationSettingsStatus("Settings synchronized from MQTT.", "success");
        }
        return;
    }

    if (topic === TOPICS.soil) byId("soil").textContent = value;
    else if (topic === TOPICS.soilPercent) byId("soilPercent").textContent = Number.isFinite(Number(value)) ? Number(value).toFixed(1) : "--";
    else if (topic === TOPICS.temperature) byId("temperature").textContent = Number.isFinite(Number(value)) ? Number(value).toFixed(1) : "--";
    else if (topic === TOPICS.humidity) byId("humidity").textContent = Number.isFinite(Number(value)) ? Number(value).toFixed(1) : "--";
    else if (topic === TOPICS.light) byId("light").textContent = value;
    else if (topic === TOPICS.water) byId("waterRaw").textContent = value;
    else if (topic === TOPICS.waterPercent) {
        const percent = Math.max(0, Math.min(100, Number(value)));
        if (Number.isFinite(percent)) {
            byId("waterPercent").textContent = percent.toFixed(1);
            byId("tankFillBar").style.height = `${percent}%`;
            byId("tankPercentText").textContent = `${percent.toFixed(0)}%`;
        }
    } else if (topic === TOPICS.pumpRuntime) {
        const seconds = Number(value);
        if (Number.isFinite(seconds)) byId("pumpRuntime").textContent = Math.round(seconds);
    } else if (topic === TOPICS.estimatedWater) {
        const litres = Number(value);
        if (Number.isFinite(litres)) byId("estimatedWater").textContent = litres.toFixed(3);
    } else if (topic === TOPICS.lowWater) {
        byId("waterFeedback").textContent = upper === "TRUE" ? "LOW WATER" : "NORMAL";
    } else if (topic === TOPICS.pumpLock) {
        currentPumpLocked = upper === "LOCKED";
        byId("pumpLockFeedback").textContent = currentPumpLocked ? "LOCKED" : "CLEAR";
        const badge = byId("pumpLockFeedbackBadge");
        badge.textContent = currentPumpLocked ? "PUMP LOCKED" : "LOCK CLEAR";
        badge.className = `status-pill ${currentPumpLocked ? "pill-lock" : "pill-on"}`;
        byId("waterFeedback").textContent = currentPumpLocked ? "LOW WATER" : "NORMAL";
        renderAlertRibbon();
    } else if (topic === TOPICS.irrigationRule) byId("irrigationRule").textContent = value;
    else if (topic === TOPICS.rainProbability) byId("rainProbability").textContent = Number.isFinite(Number(value)) ? Number(value).toFixed(0) : value;
    else if (topic === TOPICS.rainExpected) byId("rainExpected").textContent = upper === "TRUE" ? "YES" : upper === "FALSE" ? "NO" : value;
    else if (topic === TOPICS.pump) updateActuator("pump", value);
    else if (topic === TOPICS.fan) updateActuator("fan", value);
    else if (topic === TOPICS.led) updateActuator("led", value);
    else if (topic === TOPICS.pumpMode) syncModeUi("pump", value);
    else if (topic === TOPICS.fanMode) syncModeUi("fan", value);
    else if (topic === TOPICS.ledMode) syncModeUi("led", value);
    else if (topic === TOPICS.systemAlert) {
        currentSystemAlert = value;
        byId("systemAlert").textContent = value;
        renderAlertRibbon();
    } else if (topic === TOPICS.weatherStatus && upper !== "OK") {
        currentSystemAlert = `Weather service: ${value}. Local ESP32 automation remains available.`;
        renderAlertRibbon();
    } else if (topic === TOPICS.module4Online) {
        setModule4ServiceStatus(upper === "ONLINE");
    } else if (topic === TOPICS.module4Status) {
        setModule4ServiceStatus(true);
        const badge = byId("cropRiskLevel");
        badge.textContent = `RISK ${upper}`;
        badge.className = `status-pill ${upper === "GREEN" ? "pill-on" : upper === "AMBER" ? "pill-delay" : "pill-lock"}`;
    } else if (topic === TOPICS.module4Alert) {
        setModule4ServiceStatus(true);
        byId("cropAlert").textContent = value;
    } else if (topic === TOPICS.module4IntervalStatus) {
        setModule4ServiceStatus(true);
        const seconds = Number(value);
        const select = byId("cropIntervalSelect");
        if (Number.isFinite(seconds) && [...select.options].some(option => Number(option.value) === seconds)) select.value = String(seconds);
    }
    updateLcdSimulation();
}

function startMqtt() {
    if (typeof mqtt === "undefined") {
        setConnectionState("MQTT library unavailable", false);
        appendMqttActivity("MQTT.js could not be loaded. Check the internet connection and refresh the page.");
        return;
    }

    client = mqtt.connect(MQTT_URL, {
        clientId: `AgriSense_Web_${Math.random().toString(16).slice(2, 10)}`,
        clean: true,
        keepalive: 30,
        connectTimeout: 20000,
        reconnectPeriod: 3000,
        resubscribe: true,
        protocolVersion: 4
    });

    client.on("connect", () => {
        setConnectionState("HiveMQ Connected", true);
        lastNodeRedHeartbeatAt = 0;
        setNodeRedStatus("Node-RED: Checking...", false);
        appendMqttActivity("Connected to HiveMQ and subscribed to project topics.");
        client.subscribe(`${MQTT_BASE}/#`, { qos: 0 });
    });
    client.on("message", handleMqttMessage);
    client.on("reconnect", () => setConnectionState("Reconnecting...", false));
    client.on("offline", () => setConnectionState("Offline — retrying...", false));
    client.on("close", () => setConnectionState("Disconnected — retrying...", false));
    client.on("error", error => {
        setConnectionState("Connection error — retrying...", false);
        appendMqttActivity(`MQTT error: ${error.message}`);
    });
}

window.addEventListener("DOMContentLoaded", () => {
    for (const actuator of Object.keys(actuatorModes)) syncModeUi(actuator, actuatorModes[actuator]);
    for (const setting of AUTOMATION_CONFIG) byId(setting.id)?.addEventListener("input", updateAutomationSummaries);
    updateAutomationSummaries();
    updateLcdSimulation();
    window.setInterval(checkNodeRedHeartbeat, 5000);
    startMqtt();
});
