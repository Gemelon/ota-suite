"use strict";

const byId = (id) => document.getElementById(id);
let csrfToken = "";
let currentStatus = null;
let sending = false;
let statusGeneration = 0;

function showMessage(id, message, isError = false) {
  const element = byId(id);
  element.textContent = message;
  element.className = `alert ${isError ? "alert-danger" : "alert-info"}`;
}

function updateButtons() {
  byId("ota-start").disabled = sending || !currentStatus?.otaReady ||
    !currentStatus?.updateAvailable || currentStatus?.updateStale || currentStatus?.updateChecking;
  byId("update-check").disabled = sending || !currentStatus?.updateConfigured ||
    !currentStatus?.connected || currentStatus?.configuring || currentStatus?.otaBusy ||
    currentStatus?.updateChecking || (!currentStatus?.otaInsecureHttp && !currentStatus?.timeReady);
  byId("wifi-save").disabled = sending || !currentStatus ||
    currentStatus.otaBusy || currentStatus.configuring;
}

function render(status) {
  csrfToken = status.csrfToken;
  currentStatus = status;
  byId("version").textContent = status.version;
  byId("project").textContent = status.project;
  byId("network").textContent = status.connected ? status.ssid : "Nicht verbunden";
  byId("ip").textContent = status.ip || "-";
  byId("ap-ssid").textContent = status.apSsid;
  byId("time").textContent = status.timeReady ? "Synchronisiert" : "Warte auf Zeitserver";
  byId("network-message").textContent = status.networkMessage;
  showMessage("update-message", status.updateMessage, status.updateStale);
  byId("update-version").textContent = status.updateAvailable ?
    `Verfuegbar: ${status.availableVersion}` :
    (status.updateStale ? "Kandidat veraltet. Erneut pruefen." : "Keine neuere Firmware verfuegbar.");
  byId("update-notes").textContent = status.updateNotes || "";
  byId("update-last-success").textContent = status.updateLastSuccess ?
    `Letzte erfolgreiche Pruefung: ${new Date(status.updateLastSuccess * 1000).toLocaleString("de-DE")}` :
    (status.updateLastSuccessUptime ? `Letzte erfolgreiche Pruefung: ${status.updateLastSuccessUptime} s nach Start (Uhrzeit nicht synchronisiert).` :
      "Noch keine erfolgreiche Pruefung.");
  showMessage("ota-message", status.otaMessage, status.otaState === "failed");
  const transportWarning = byId("ota-transport-warning");
  transportWarning.textContent = status.otaInsecureHttp ?
    "HTTP-Testmodus: keine Verschluesselung und keine Pruefung der Serveridentitaet. " +
    "Firmware kann im Netzwerk manipuliert werden. Nur im vertrauenswuerdigen Test-LAN verwenden." : "";
  transportWarning.className = status.otaInsecureHttp ?
    "alert alert-warning" : "alert alert-warning d-none";
  const requirements = [];
  if (!status.otaConfigured) requirements.push("Firmware-Adresse fehlt, ist ungueltig oder HTTP ist nicht erlaubt.");
  if (!status.bootConfirmed) requirements.push("Startpruefung laeuft (ca. 10 Sekunden).");
  if (!status.connected) requirements.push("WLAN-Verbindung erforderlich.");
  if (!status.timeReady && !status.otaInsecureHttp) {
    requirements.push("Uhrzeit fuer Zertifikatspruefung noch nicht synchronisiert.");
  }
  if (status.configuring) requirements.push("WLAN-Konfiguration laeuft.");
  byId("ota-requirement").textContent = requirements.join(" ");
  const progress = byId("ota-progress");
  if (status.otaBusy && status.totalBytes === 0) {
    progress.removeAttribute("value");
  } else {
    progress.value = status.totalBytes > 0 ?
      Math.min(100, Math.floor(100 * status.bytesRead / status.totalBytes)) : 0;
  }
  byId("ota-bytes").textContent = status.bytesRead > 0 ?
    `${status.bytesRead.toLocaleString("de-DE")} Bytes geladen` +
    (status.totalBytes > 0 ? ` von ${status.totalBytes.toLocaleString("de-DE")}` : "") :
    "Noch keine Firmware-Daten geladen.";
  updateButtons();
}

async function refresh() {
  const generation = statusGeneration;
  try {
    const response = await fetch("/api/status", {
      cache: "no-store",
      signal: AbortSignal.timeout(8000),
    });
    if (!response.ok) throw new Error(await response.text());
    const status = await response.json();
    if (!sending && generation === statusGeneration) {
      render(status);
      byId("connection-error").classList.add("d-none");
    }
  } catch (error) {
    if (!sending && generation === statusGeneration) {
      const restarting = currentStatus?.otaState === "rebooting";
      currentStatus = null;
      updateButtons();
      showMessage("connection-error", restarting ?
        "Geraet startet neu. Verbindung wird erneut versucht." :
        `Geraet nicht erreichbar: ${error.message}. Verbindung / Anmeldung pruefen.`, !restarting);
    }
  } finally {
    setTimeout(refresh, 1500);
  }
}

async function sendAction(url, body, version) {
  sending = true;
  ++statusGeneration;
  updateButtons();
  try {
    const headers = { "X-CSRF-Token": csrfToken };
    if (version !== undefined) headers["X-Update-Version"] = version;
    if (body !== undefined) headers["Content-Type"] = "application/json";
    const response = await fetch(url, {
      method: "POST",
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(10000),
    });
    const message = await response.text();
    if (!response.ok) throw new Error(message);
    showMessage("action-result", message);
    currentStatus = null;
  } catch (error) {
    showMessage("action-result",
      `Anfrage fehlgeschlagen: ${error.message}. Geraetestatus pruefen; nicht automatisch wiederholen.`, true);
    currentStatus = null;
  } finally {
    ++statusGeneration;
    sending = false;
    updateButtons();
  }
}

byId("ota-start").addEventListener("click", () => {
  if (sending || !currentStatus?.otaReady || !currentStatus?.updateAvailable ||
      currentStatus?.updateStale || currentStatus?.updateChecking) return;
  const version = currentStatus.availableVersion;
  const transport = currentStatus.otaInsecureHttp ?
    "HTTP-Testadresse (unverschluesselt, Serveridentitaet ungeprueft)" : "HTTPS-Adresse";
  if (confirm(`Firmware ${version} von der konfigurierten ${transport} laden und installieren? ` +
              "Nach erfolgreicher Pruefung erfolgt ein Neustart.")) {
    void sendAction("/api/ota", undefined, version);
  }
});

byId("update-check").addEventListener("click", () => {
  if (byId("update-check").disabled) return;
  void sendAction("/api/updates/check");
});

byId("wifi-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (sending || !currentStatus || currentStatus.otaBusy || currentStatus.configuring) return;
  const ssid = byId("ssid").value;
  const password = byId("password").value;
  const encoder = new TextEncoder();
  const ssidLength = encoder.encode(ssid).length;
  const passwordLength = encoder.encode(password).length;
  if (ssidLength < 1 || ssidLength > 32 ||
      (passwordLength !== 0 && (passwordLength < 8 || passwordLength > 64)) ||
      (passwordLength === 64 && !/^[0-9a-fA-F]{64}$/.test(password))) {
    showMessage("action-result", "SSID: 1-32 Bytes; Passwort: leer, 8-63 Bytes oder 64 Hex-Zeichen.", true);
    return;
  }
  byId("password").value = "";
  void sendAction("/api/network", { ssid, password });
});

void refresh();
