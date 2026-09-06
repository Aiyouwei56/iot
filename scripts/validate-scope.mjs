import fs from "node:fs";

const repoRoot = new URL("../", import.meta.url);
const readRepoFile = file => fs.readFileSync(new URL(file.replaceAll("\\", "/"), repoRoot), "utf8");

for (const file of ["index.html", "simulator.html"]) {
  const html = readRepoFile(file);
  const blocks = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/gi)]
    .map(match => match[1]);
  blocks.forEach((code, index) => {
    try {
      new Function(code);
    } catch (error) {
      throw new Error(`${file} inline script ${index + 1}: ${error.message}`);
    }
  });
  console.log(`${file}: inline JavaScript syntax OK (${blocks.length} block)`);
}

const flow = JSON.parse(readRepoFile("node-red/flows.json"));
const functionNodes = flow.filter(node => node.type === "function");
for (const node of functionNodes) {
  try {
    new Function("msg", "flow", "env", "node", node.func);
  } catch (error) {
    throw new Error(`Node-RED ${node.id}: ${error.message}`);
  }
}
console.log(`Node-RED JSON and function syntax OK (${functionNodes.length} functions)`);

const requiredTopics = [
  "module1/soil", "module1/soil_percent", "module1/pump", "module1/pump/mode",
  "module2/temperature", "module2/humidity", "module2/light", "module2/fan",
  "module2/led", "module3/water", "module3/water_percent", "module3/pump_lock",
  "module3/pump_runtime_seconds", "module3/estimated_water_litres",
  "control/pump", "control/pump/mode", "control/pump/auto",
  "control/fan", "control/fan/mode", "control/led", "control/led/mode",
  "module4/yellowing", "module4/risk", "module4/status",
  "module4/alert"
];
const contract = readRepoFile("docs/mqtt-contract.md");
const firmware = readRepoFile("firmware/smart_agriculture/smart_agriculture.ino");
const dashboard = readRepoFile("index.html") + readRepoFile("dashboard.js");
const simulator = readRepoFile("simulator.html");
const flowText = readRepoFile("node-red/flows.json");
for (const suffix of requiredTopics) {
  if (!contract.includes(suffix)) throw new Error(`MQTT contract missing ${suffix}`);
  const module4Topic = suffix.startsWith("module4/");
  const importantEverywhere = !module4Topic && !suffix.includes("soil_percent") && !suffix.includes("pump/auto");
  if (importantEverywhere && !firmware.includes(suffix)) throw new Error(`Firmware missing ${suffix}`);
  if (importantEverywhere && !dashboard.includes(suffix)) throw new Error(`Dashboard missing ${suffix}`);
  if (importantEverywhere && !simulator.includes(suffix)) throw new Error(`Simulator missing ${suffix}`);
  if (!flowText.includes(suffix)) throw new Error(`Node-RED missing ${suffix}`);
}
const module4Services = readRepoFile("module4/agrisense/services.py");
for (const suffix of requiredTopics.filter(topic => topic.startsWith("module4/"))) {
  if (!module4Services.includes(suffix)) throw new Error(`Module 4 service missing ${suffix}`);
}
console.log("Cross-component MQTT contract checks OK");

const runtimeSettings = {
  "config/soil/pump_on_percent": [dashboard, simulator, flowText, contract],
  "config/soil/pump_off_percent": [dashboard, simulator, flowText, contract],
  "config/irrigation/pump_pulse_seconds": [dashboard, simulator, firmware, contract],
  "config/fan/on_temperature_c": [dashboard, simulator, firmware, contract],
  "config/fan/off_temperature_c": [dashboard, simulator, firmware, contract],
  "config/light/dark_raw": [dashboard, simulator, firmware, contract],
  "config/water/low_percent": [dashboard, simulator, firmware, contract],
  "config/irrigation/rain_probability": [dashboard, simulator, flowText, contract],
  "config/irrigation/humidity_percent": [dashboard, simulator, flowText, contract]
};
for (const [topic, consumers] of Object.entries(runtimeSettings)) {
  if (consumers.some(text => !text.includes(topic))) {
    throw new Error(`Runtime automation setting is not aligned: ${topic}`);
  }
}
for (const [name, html] of [["index.html", dashboard], ["simulator.html", simulator]]) {
  if (/\p{Script=Han}/u.test(html)) throw new Error(`${name} still contains Chinese text`);
}
console.log("Runtime settings and English-only webpage checks OK");

const activeFiles = [
  "index.html", "dashboard.js", "simulator.html", "supabase-dashboard.js", "node-red/flows.json",
  "module4/agrisense/analysis.py", "module4/agrisense/risk.py",
  "module4/agrisense/services.py", "module4/agrisense/app.py"
];
const forbidden = /\b(weight_raw|tank_weight|nutrient_weight|HX711|DOSING_PUMP)\b/i;
for (const file of activeFiles) {
  if (forbidden.test(readRepoFile(file))) throw new Error(`Out-of-scope field remains in ${file}`);
}
console.log("Out-of-scope active-code checks OK");
