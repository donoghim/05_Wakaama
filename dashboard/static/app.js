const services = ["bootstrap", "server"];
let originalIni = "";
let firmwareArtifacts = [];

function setServiceState(name, status) {
  const state = document.getElementById(`${name}-state`);
  const process = document.getElementById(`${name}-process`);
  const healthy = status.running && status.listener;
  state.textContent = healthy ? "Online" : "Attention";
  state.classList.toggle("online", healthy);
  state.classList.toggle("attention", !healthy);
  process.textContent = status.processes.length ? status.processes.join("\n") :
    `Process not found. UDP ${status.port}: ${status.listener ? "listening" : "not listening"}`;
}

async function controlService(target, action) {
  const label = target === "bootstrap" ? "Bootstrap Server" : "LwM2M Server";
  if ((action === "stop" || action === "restart") && !window.confirm(`${action} ${label}?`)) return;
  const response = await fetch(`/api/services/${target}/${action}`, { method: "POST" });
  const payload = await response.json();
  if (!response.ok) {
    document.getElementById("refresh-status").textContent = payload.error;
    return;
  }
  document.getElementById("refresh-status").textContent = `${label}: ${action} requested`;
  window.setTimeout(refresh, 350);
}

function updateLog(name, log) {
  const element = document.getElementById(`${name}-log`);
  const atBottom = element.scrollTop + element.clientHeight >= element.scrollHeight - 8;
  element.textContent = log.contents || "No log output yet.";
  document.getElementById(`${name}-log-path`).textContent = `Log: ${log.path}`;
  if (atBottom) element.scrollTop = element.scrollHeight;
}

function updateClients(payload) {
  const source = document.getElementById("clients-source");
  const body = document.getElementById("clients-table");
  source.textContent = payload.source;
  body.replaceChildren();
  if (!payload.clients.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 5;
    cell.textContent = "No registered client blocks found in the current server output.";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }
  payload.clients.forEach((client) => {
    const row = document.createElement("tr");
    [client.id, client.name, client.binding, client.lifetime, client.objects].forEach((value) => {
      const cell = document.createElement("td");
      cell.textContent = value === "" || value === null || value === undefined ? "-" : value;
      row.appendChild(cell);
    });
    body.appendChild(row);
  });
  const endpointSelect = document.getElementById("write-endpoint");
  const previousValue = endpointSelect.value;
  endpointSelect.replaceChildren(...payload.clients.map((client) => new Option(`${client.name} (#${client.id})`, client.name)));
  if ([...endpointSelect.options].some((option) => option.value === previousValue)) endpointSelect.value = previousValue;
  const dfotaSelect = document.getElementById("dfota-endpoint");
  const previousDfotaValue = dfotaSelect.value;
  dfotaSelect.replaceChildren(...payload.clients.map((client) => new Option(`${client.name} (#${client.id})`, client.name)));
  if ([...dfotaSelect.options].some((option) => option.value === previousDfotaValue)) dfotaSelect.value = previousDfotaValue;
}

function setWriteMessage(message, isError = false) {
  const state = document.getElementById("write-state");
  state.textContent = message;
  state.classList.toggle("error", isError);
}

function setDfotaMessage(message, isError = false) {
  const state = document.getElementById("dfota-state");
  state.textContent = message;
  state.classList.toggle("error", isError);
}

function updateFirmwareHash() {
  const artifact = firmwareArtifacts.find((item) => item.name === document.getElementById("firmware-file").value);
  document.getElementById("firmware-hash").textContent = artifact ? `${artifact.size} bytes  ${artifact.sha256}` : "No approved artifact selected.";
}

async function loadFirmware() {
  const response = await fetch("/api/firmware", { cache: "no-store" });
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error);
  firmwareArtifacts = payload.artifacts;
  const select = document.getElementById("firmware-file");
  select.replaceChildren(...firmwareArtifacts.map((artifact) => new Option(artifact.name, artifact.name)));
  updateFirmwareHash();
}

async function uploadFirmware() {
  const input = document.getElementById("firmware-upload");
  const file = input.files[0];
  if (!file) { setDfotaMessage("Select a firmware file to upload.", true); return; }
  const form = new FormData();
  form.append("firmware", file, file.name);
  setDfotaMessage(`Uploading ${file.name}...`);
  const response = await fetch("/api/firmware/upload", { method: "POST", body: form });
  const payload = await response.json();
  if (!response.ok) { setDfotaMessage(payload.error, true); return; }
  input.value = "";
  await loadFirmware();
  document.getElementById("firmware-file").value = payload.name;
  updateFirmwareHash();
  setDfotaMessage(`Uploaded ${payload.name} (${payload.size} bytes).`);
}

async function refresh() {
  const marker = document.getElementById("refresh-status");
  try {
    const [statusResponse, logsResponse, clientsResponse] = await Promise.all([
      fetch("/api/status", { cache: "no-store" }), fetch("/api/logs", { cache: "no-store" }),
      fetch("/api/clients", { cache: "no-store" }),
    ]);
    if (!statusResponse.ok || !logsResponse.ok || !clientsResponse.ok) throw new Error("Dashboard API unavailable");
    const status = await statusResponse.json();
    const logs = await logsResponse.json();
    const clients = await clientsResponse.json();
    services.forEach((name) => { setServiceState(name, status[name]); updateLog(name, logs[name]); });
    updateClients(clients);
    marker.textContent = `Updated ${new Date().toLocaleTimeString()}`;
  } catch (error) {
    marker.textContent = "Dashboard connection failed";
  }
}

function setIniMessage(message, isError = false) {
  const element = document.getElementById("ini-state");
  element.textContent = message;
  element.classList.toggle("error", isError);
}

function simpleDiff(before, after) {
  if (before === after) return "No pending changes.";
  const beforeLines = new Set(before.split("\n"));
  const afterLines = new Set(after.split("\n"));
  return [
    ...before.split("\n").filter((line) => !afterLines.has(line)).map((line) => `- ${line}`),
    ...after.split("\n").filter((line) => !beforeLines.has(line)).map((line) => `+ ${line}`),
  ].join("\n");
}

async function loadIni() {
  const response = await fetch("/api/bootstrap/ini", { cache: "no-store" });
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error);
  originalIni = payload.contents;
  document.getElementById("ini-editor").value = originalIni;
  const select = document.getElementById("server-id");
  select.replaceChildren(...payload.server_ids.map((serverId) => new Option(`Server ${serverId}`, serverId)));
  document.getElementById("restart-bootstrap").disabled = !payload.restart_configured;
  setIniMessage(payload.restart_configured ? "Ready" : "Restart command not configured");
}

document.getElementById("add-endpoint").addEventListener("click", () => {
  const name = document.getElementById("endpoint-name").value.trim();
  const serverId = document.getElementById("server-id").value;
  if (!/^[A-Za-z0-9._-]{1,128}$/.test(name)) {
    setIniMessage("EPNS may use letters, numbers, dot, underscore, and hyphen.", true);
    return;
  }
  const editor = document.getElementById("ini-editor");
  editor.value = `${editor.value.replace(/\s*$/, "")}\n\n[Endpoint]\nName=${name}\nServer=${serverId}\n`;
  document.getElementById("endpoint-name").value = "";
  document.getElementById("ini-diff").textContent = simpleDiff(originalIni, editor.value);
  setIniMessage("Endpoint added to pending configuration.");
});

document.getElementById("preview-ini").addEventListener("click", () => {
  document.getElementById("ini-diff").textContent = simpleDiff(originalIni, document.getElementById("ini-editor").value);
});

document.getElementById("save-ini").addEventListener("click", async () => {
  const response = await fetch("/api/bootstrap/ini", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ contents: document.getElementById("ini-editor").value }) });
  const payload = await response.json();
  if (!response.ok) { setIniMessage(payload.error, true); return; }
  originalIni = document.getElementById("ini-editor").value;
  document.getElementById("ini-diff").textContent = "No pending changes.";
  setIniMessage(`Applied. Backup: ${payload.backup}`);
  await loadIni();
});

document.getElementById("restart-bootstrap").addEventListener("click", async () => {
  if (!window.confirm("Restart Bootstrap Server now?")) return;
  const response = await fetch("/api/bootstrap/restart", { method: "POST" });
  const payload = await response.json();
  setIniMessage(response.ok ? "Restart command completed." : payload.error, !response.ok);
});

document.getElementById("queue-write").addEventListener("click", async () => {
  const endpoint = document.getElementById("write-endpoint").value;
  const uri = document.getElementById("write-uri").value.trim();
  const value = document.getElementById("write-value").value;
  if (!endpoint) { setWriteMessage("Select a registered endpoint.", true); return; }
  const response = await fetch("/api/write", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ endpoint, uri, value }) });
  const payload = await response.json();
  if (!response.ok) { setWriteMessage(payload.error, true); return; }
  setWriteMessage(`Queued for client #${payload.client_id} at ${new Date(payload.queued_at).toLocaleTimeString()}`);
});

document.getElementById("firmware-file").addEventListener("change", updateFirmwareHash);
document.getElementById("upload-firmware").addEventListener("click", uploadFirmware);
document.getElementById("queue-dfota").addEventListener("click", async () => {
  const endpoint = document.getElementById("dfota-endpoint").value;
  const filename = document.getElementById("firmware-file").value;
  if (!endpoint || !filename) { setDfotaMessage("Select an endpoint and firmware artifact.", true); return; }
  if (!window.confirm(`Deploy ${filename} to ${endpoint}?`)) return;
  const response = await fetch("/api/dfota", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ endpoint, filename }) });
  const payload = await response.json();
  if (!response.ok) { setDfotaMessage(payload.error, true); return; }
  setDfotaMessage(`Queued for client #${payload.client_id} at ${new Date(payload.queued_at).toLocaleTimeString()}`);
});

document.querySelectorAll(".service-action").forEach((button) => {
  button.addEventListener("click", () => controlService(button.dataset.target, button.dataset.action));
});

document.querySelectorAll(".panel-toggle").forEach((toggle) => {
  const content = document.getElementById(toggle.getAttribute("aria-controls"));
  const setExpanded = (expanded) => {
    toggle.setAttribute("aria-expanded", String(expanded));
    content.hidden = !expanded;
  };

  const togglePanel = () => setExpanded(toggle.getAttribute("aria-expanded") !== "true");
  toggle.addEventListener("click", togglePanel);
  toggle.addEventListener("keydown", (event) => {
    if (event.key !== "Enter" && event.key !== " ") return;
    event.preventDefault();
    togglePanel();
  });
});

loadIni().catch((error) => setIniMessage(error.message, true));
loadFirmware().catch((error) => setDfotaMessage(error.message, true));

refresh();
window.setInterval(refresh, 1500);