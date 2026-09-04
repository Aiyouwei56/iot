// Supabase publishable keys are designed for browser use. Database safety is
// enforced by Row Level Security; never place a service-role key in this file.
const SUPABASE_URL = "https://vysijpetcmetbbtcipmp.supabase.co";
const SUPABASE_PUBLISHABLE_KEY = "sb_publishable_NFo0n7MgjnUT7CrIfDBnHg_V7q-XTIh";

const SUPABASE_HEADERS = {
    apikey: SUPABASE_PUBLISHABLE_KEY,
    Authorization: `Bearer ${SUPABASE_PUBLISHABLE_KEY}`
};

let historyChart = null;
let currentCropObjectUrl = null;
let bridgeCloudStatus = null;

async function supabaseSelect(table, query) {
    const response = await fetch(
        `${SUPABASE_URL}/rest/v1/${table}?${query}`,
        { headers: SUPABASE_HEADERS }
    );

    if (!response.ok) {
        throw new Error(`${table}: HTTP ${response.status}`);
    }

    return response.json();
}

function displayValue(value, decimals = 1) {
    if (value === null || value === undefined || value === "") {
        return "--";
    }

    const number = Number(value);
    return Number.isFinite(number) ? number.toFixed(decimals) : String(value);
}

function displayTime(value) {
    if (!value) return "No recent data";

    return new Intl.DateTimeFormat("en-MY", {
        dateStyle: "short",
        timeStyle: "medium"
    }).format(new Date(value));
}

function escapeHtml(value) {
    return String(value ?? "")
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}

function setCloudConnection(text, state) {
    const element = document.getElementById("cloudConnection");
    element.textContent = text;
    element.className = `cloud-state ${state}`;
}

function updateQueueCount(value) {
    const count = Number(value);
    document.getElementById("cloudQueueCount").textContent = Number.isFinite(count)
        ? String(Math.max(0, count))
        : "--";
}

function renderBridgeCloudStatus(payload) {
    bridgeCloudStatus = payload;
    updateQueueCount(payload.queue);

    if (payload.lastSuccess) {
        document.getElementById("cloudLastSync").textContent =
            `Last Node-RED sync: ${displayTime(payload.lastSuccess)}`;
    }

    if (payload.status === "OFFLINE") {
        setCloudConnection("Cloud Offline — Live MQTT Active", "warning");
        document.getElementById("cloudUpdated").textContent =
            payload.lastError || "Records are safely queued on the laptop.";
    } else if (payload.status === "ONLINE") {
        setCloudConnection(payload.queue > 0 ? "Connected — Synchronising" : "Connected", "healthy");
    }
}

function connectNodeRedStatusTopics() {
    if (typeof client === "undefined") return;

    const topics = [`${MQTT_BASE}/system/cloud`, `${MQTT_BASE}/system/queue`];
    const subscribe = () => client.subscribe(topics);

    if (client.connected) subscribe();
    client.on("connect", subscribe);
    client.on("message", (topic, message) => {
        if (topic === `${MQTT_BASE}/system/queue`) {
            updateQueueCount(message.toString());
            return;
        }

        if (topic === `${MQTT_BASE}/system/cloud`) {
            try {
                renderBridgeCloudStatus(JSON.parse(message.toString()));
            } catch (error) {
                console.warn("Invalid Node-RED cloud status:", error);
            }
        }
    });
}

function renderModuleStatus(modules) {
    for (let moduleId = 1; moduleId <= 4; moduleId += 1) {
        const module = modules.find(item => Number(item.module_id) === moduleId);
        const status = module?.health_status ?? "OFFLINE";
        const statusElement = document.getElementById(`moduleStatus${moduleId}`);
        const timeElement = document.getElementById(`moduleTime${moduleId}`);

        statusElement.textContent = status;
        statusElement.className = `cloud-state ${status.toLowerCase()}`;
        timeElement.textContent = displayTime(module?.last_seen);
    }
}

function renderSensorTable(records) {
    const body = document.getElementById("sensorHistoryBody");

    if (!records.length) {
        body.innerHTML = '<tr><td colspan="11">No sensor history yet. Start Node-RED and publish MQTT data.</td></tr>';
        return;
    }

    body.innerHTML = records.slice(0, 10).map(record => `
        <tr>
            <td>${escapeHtml(displayTime(record.recorded_at))}</td>
            <td>${escapeHtml(displayValue(record.soil_percent))}% (${escapeHtml(displayValue(record.soil_raw, 0))})</td>
            <td>${escapeHtml(displayValue(record.temperature_c))}</td>
            <td>${escapeHtml(displayValue(record.humidity_percent))}</td>
            <td>${escapeHtml(displayValue(record.water_raw, 0))}</td>
            <td>${escapeHtml(displayValue(record.water_level_percent))}</td>
            <td>${escapeHtml(displayValue(record.light_raw, 0))}</td>
            <td>${escapeHtml(record.pump_state)}</td>
            <td>${escapeHtml(record.fan_state)}</td>
            <td>${escapeHtml(record.led_state)}</td>
            <td>${escapeHtml(record.data_quality)}</td>
        </tr>
    `).join("");
}

function renderSensorChart(records) {
    if (typeof Chart === "undefined") return;

    const ordered = [...records].reverse();
    const data = {
        labels: ordered.map(record => new Date(record.recorded_at).toLocaleTimeString("en-MY")),
        datasets: [
            {
                label: "Temperature °C",
                data: ordered.map(record => record.temperature_c),
                borderColor: "#ef4444",
                backgroundColor: "rgba(239, 68, 68, 0.12)",
                tension: 0.25
            },
            {
                label: "Humidity %",
                data: ordered.map(record => record.humidity_percent),
                borderColor: "#3b82f6",
                backgroundColor: "rgba(59, 130, 246, 0.12)",
                tension: 0.25
            }
        ]
    };

    if (historyChart) {
        historyChart.data = data;
        historyChart.update();
        return;
    }

    historyChart = new Chart(document.getElementById("sensorHistoryChart"), {
        type: "line",
        data,
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: { intersect: false, mode: "index" },
            scales: { y: { beginAtZero: false } }
        }
    });
}

function renderAlerts(alerts) {
    const body = document.getElementById("alertHistoryBody");

    if (!alerts.length) {
        body.innerHTML = '<tr><td colspan="4">No open alerts.</td></tr>';
        return;
    }

    body.innerHTML = alerts.map(alert => `
        <tr>
            <td>${escapeHtml(displayTime(alert.created_at))}</td>
            <td>${escapeHtml(alert.module_id ?? "System")}</td>
            <td>${escapeHtml(alert.severity)}</td>
            <td>${escapeHtml(alert.message)}</td>
        </tr>
    `).join("");
}

async function loadPrivateCropImage(imagePath) {
    const image = document.getElementById("cropImage");
    const message = document.getElementById("cropImageMessage");

    if (!imagePath) {
        image.style.display = "none";
        message.textContent = "No image available.";
        return;
    }

    const safePath = imagePath.split("/").map(encodeURIComponent).join("/");
    const response = await fetch(
        `${SUPABASE_URL}/storage/v1/object/authenticated/crop-images/${safePath}`,
        { headers: SUPABASE_HEADERS }
    );

    if (!response.ok) {
        image.style.display = "none";
        message.textContent = `Image unavailable (HTTP ${response.status}).`;
        return;
    }

    if (currentCropObjectUrl) URL.revokeObjectURL(currentCropObjectUrl);
    currentCropObjectUrl = URL.createObjectURL(await response.blob());
    image.src = currentCropObjectUrl;
    image.style.display = "block";
    message.textContent = imagePath;
}

async function renderCropHealth(records) {
    if (!records.length) return;

    const record = records[0];
    const level = record.risk_level ?? "GREEN";
    const badge = document.getElementById("cropRiskLevel");
    badge.textContent = level;
    badge.className = `risk-badge ${level.toLowerCase()}`;

    document.getElementById("cropRiskScore").textContent = displayValue(record.risk_score);
    document.getElementById("cropGreen").textContent = `${displayValue(record.green_percent)}%`;
    document.getElementById("cropYellowing").textContent = `${displayValue(record.yellowing_percent)}%`;
    document.getElementById("cropCamera").textContent = record.camera_status;
    document.getElementById("cropAlert").textContent = record.alert_message || "No crop-health alert.";
    document.getElementById("cropCaptured").textContent = `Captured: ${displayTime(record.captured_at)}`;

    await loadPrivateCropImage(record.image_path);
}

async function refreshCloudDashboard() {
    try {
        const [modules, sensors, cropHealth, alerts] = await Promise.all([
            supabaseSelect("module_status", "select=module_id,health_status,last_seen&order=module_id"),
            supabaseSelect(
                "sensor_data",
                "select=soil_raw,soil_percent,temperature_c,humidity_percent,water_raw,water_level_percent,light_raw,pump_state,fan_state,led_state,data_quality,recorded_at&order=recorded_at.desc&limit=20"
            ),
            supabaseSelect(
                "crop_health_records",
                "select=image_path,green_percent,yellowing_percent,risk_score,risk_level,camera_status,alert_message,captured_at&order=captured_at.desc&limit=1"
            ),
            supabaseSelect(
                "alerts",
                "select=module_id,severity,message,created_at&acknowledged=eq.false&order=created_at.desc&limit=10"
            )
        ]);

        renderModuleStatus(modules);
        renderSensorTable(sensors);
        renderSensorChart(sensors);
        renderAlerts(alerts);
        await renderCropHealth(cropHealth);

        setCloudConnection("Connected", "healthy");
        document.getElementById("cloudUpdated").textContent = `Updated: ${displayTime(new Date())}`;

        if (bridgeCloudStatus?.status === "OFFLINE") {
            renderBridgeCloudStatus(bridgeCloudStatus);
        }
    } catch (error) {
        console.error("Supabase dashboard error:", error);
        setCloudConnection("Connection Error", "fault");
        document.getElementById("cloudUpdated").textContent = error.message;
    }
}

window.addEventListener("DOMContentLoaded", () => {
    // Keep the original Modules 1-3 markup untouched, but display the new
    // cloud and Module 4 sections after them.
    const cloudSections = document.getElementById("cloudSections");
    document.querySelector("main").appendChild(cloudSections);

    connectNodeRedStatusTopics();
    refreshCloudDashboard();
    window.setInterval(refreshCloudDashboard, 15000);
});
