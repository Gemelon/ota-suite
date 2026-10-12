"use strict";

const byId = (id) => document.getElementById(id);
let csrfToken = "";
let currentStatus = null;
let sending = false;
let statusGeneration = 0;
let activeTab = "overview";
let initialTabApplied = false;
let userSelectedTab = false;
const loadedExtensions = new Set();
const loadingExtensions = new Set();
let primaryTabs = new Set();
const sectionTabs = { primary: null, administration: null };
const iconPaths = {
  resistance: "M2 12h3l2-4 4 8 4-8 2 4h5",
  dashboard: "M3 3v18h18M7 16l4-5 4 3 6-9",
  battery: "M3 7h17v10H3zM20 10h2v4h-2M7 10v4M5 12h4M13 12h4",
  manual: "M5 3v18M12 3v18M19 3v18M2 8h6M9 16h6M16 10h6",
  auto: "M9 5l11 7-11 7zM4 5v14",
  overview: "M12 11v6M12 7h.01M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0",
  "sd-card": "M5 3h10l4 4v14H5zM8 3v6h8M8 15h8M8 18h8",
  updates: "M12 16V3M7 8l5-5 5 5M4 15v6h16v-6",
  settings: "M5 3v18M12 3v18M19 3v18M2 8h6M9 16h6M16 10h6",
};

function addIcon(element, id) {
  if (!element || element.querySelector(".ui-icon")) return;
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("class", "ui-icon");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("fill", "none");
  svg.setAttribute("stroke", "currentColor");
  svg.setAttribute("stroke-width", "1.8");
  svg.setAttribute("stroke-linecap", "round");
  svg.setAttribute("stroke-linejoin", "round");
  const path = document.createElementNS(svg.namespaceURI, "path");
  path.setAttribute("d", iconPaths[id] || iconPaths.settings);
  svg.append(path);
  element.prepend(svg);
}

function decoratePane(pane, id) {
  pane.querySelectorAll("h2, h3").forEach((heading) => addIcon(heading, id));
}

function tabSection(id) {
  return primaryTabs.has(id) ? "primary" : "administration";
}

function updateSections() {
  const grouped = primaryTabs.size > 0;
  byId("section-menu").classList.toggle("d-none", !grouped);
  const section = tabSection(activeTab);
  for (const button of byId("web-tabs").querySelectorAll("[data-tab-target]")) {
    button.parentElement.classList.toggle("d-none", grouped && tabSection(button.dataset.tabTarget) !== section);
    addIcon(button, button.dataset.tabTarget);
  }
  for (const button of document.querySelectorAll("[data-section]")) {
    const active = grouped && button.dataset.section === section;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", String(active));
    addIcon(button, button.dataset.section === "primary" ? "battery" : "settings");
  }
  byId("page-title").textContent = grouped ? (section === "primary" ? I18n.t("text.1fd7e1d51569") : I18n.t("text.2456bb8ed72f"))
    : currentStatus?.project || "Firmware";
  byId("page-subtitle").textContent = grouped && section === "primary"
    ? I18n.t("text.690c0f1d7edf")
    : I18n.t("text.9735448f0c23");
  addIcon(byId("page-title"), grouped && section === "primary" ? "battery" : "settings");
}

document.querySelectorAll("[data-section]").forEach((button) => {
  button.addEventListener("click", () => {
    const section = button.dataset.section;
    const target = sectionTabs[section] || [...byId("web-tabs").querySelectorAll("[data-tab-target]")]
      .find((tab) => tabSection(tab.dataset.tabTarget) === section)?.dataset.tabTarget;
    if (target) byId(`tab-button-${target}`).click();
  });
});

let activityUrl = "";
let activityFrames = null;
let activityTimer = 0;
let blinkTimer = 0;
let framesToBlink = 0;

function toggleFrameLed() {
  blinkTimer = 0;
  if (!framesToBlink) return;
  byId("frame-led").classList.toggle("lit");
  framesToBlink--;
  blinkTimer = setTimeout(toggleFrameLed, 100);
}

async function pollActivity() {
  clearTimeout(activityTimer);
  if (!activityUrl) return;
  if (!document.hidden) {
    try {
      const response = await fetch(activityUrl, {
        cache: "no-store", credentials: "same-origin", signal: AbortSignal.timeout(5000),
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const status = await response.json();
      const link = status.link;
      if (!link || !Number.isInteger(link.framesOk)) throw new Error(I18n.t("text.18c889e51ab9"));
      const fresh = link.connected && link.ageMs >= 0 && link.ageMs < 5000;
      const led = byId("frame-led");
      led.classList.toggle("receiving", fresh);
      led.classList.remove("error");
      if (fresh && activityFrames !== null && link.framesOk >= activityFrames) {
        framesToBlink += link.framesOk - activityFrames;
        if (framesToBlink && !blinkTimer) toggleFrameLed();
      }
      if (!fresh) {
        clearTimeout(blinkTimer);
        blinkTimer = 0;
        framesToBlink = 0;
        led.classList.remove("lit");
      }
      activityFrames = link.framesOk;
      byId("frame-label").textContent = fresh ? "EBC RX" : I18n.t("text.ecbc52cf53bc");
      byId("frame-indicator").title = I18n.f("text.05acb60b4d46", {p0: link.framesOk, p1: link.framesBad});
      document.dispatchEvent(new CustomEvent("tester-status", { detail: status }));
    } catch (error) {
      clearTimeout(blinkTimer);
      blinkTimer = 0;
      framesToBlink = 0;
      activityFrames = null;
      byId("frame-led").className = "frame-led error";
      byId("frame-label").textContent = I18n.t("text.be7a816047ea");
      byId("frame-indicator").title = error.message;
      document.dispatchEvent(new CustomEvent("tester-status-error"));
    }
  } else {
    activityFrames = null;
    clearTimeout(blinkTimer);
    blinkTimer = 0;
    framesToBlink = 0;
    byId("frame-led").classList.remove("lit");
  }
  activityTimer = setTimeout(pollActivity, 500);
}

function showMessage(id, message, isError = false) {
  const element = byId(id);
  element.textContent = I18n.message(message);
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

function selectTab(id) {
  const target = byId(`tab-${id}`);
  if (!target) return;
  document.querySelectorAll(".tab-pane").forEach((pane) => {
    pane.classList.toggle("d-none", pane !== target);
  });
  document.querySelectorAll("[data-tab-target]").forEach((button) => {
    const active = button.dataset.tabTarget === id;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", String(active));
  });
  activeTab = id;
  sectionTabs[tabSection(id)] = id;
  updateSections();
}

async function loadExtension(extension, pane) {
  if (loadedExtensions.has(extension.id) || loadingExtensions.has(extension.id)) return;
  loadingExtensions.add(extension.id);
  try {
    const response = await fetch(extension.contentUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(await response.text());
    pane.innerHTML = await response.text();
    I18n.apply(pane);
    decoratePane(pane, extension.id);
    const script = document.createElement("script");
    script.src = extension.scriptUrl;
    script.onload = () => {
      loadedExtensions.add(extension.id);
      loadingExtensions.delete(extension.id);
    };
    script.onerror = () => {
      loadingExtensions.delete(extension.id);
      showMessage("connection-error", I18n.f("text.5eb9bc4d3ebc", {p0: I18n.message(extension.label)}), true);
    };
    document.head.append(script);
  } catch (error) {
    loadingExtensions.delete(extension.id);
    showMessage("connection-error", I18n.f("text.b8a82f860a90", {p0: I18n.message(extension.label), p1: I18n.message(error.message)}), true);
  }
}

function renderExtensions(extensions) {
  const tabs = byId("web-tabs");
  for (const extension of extensions || []) {
    if (!/^[a-z0-9-]{1,16}$/.test(extension.id)) continue;
    if (extension.placement === "settings") {
      let pane = byId(`settings-ext-${extension.id}`);
      if (!pane) {
        pane = document.createElement("div");
        pane.id = `settings-ext-${extension.id}`;
        pane.className = "mt-4";
        byId("settings-extensions").append(pane);
      }
      if (activeTab === "settings") void loadExtension(extension, pane);
      continue;
    }
    let button = byId(`tab-button-${extension.id}`);
    let pane = byId(`tab-${extension.id}`);
    if (!button) {
      const item = document.createElement("li");
      item.className = "nav-item";
      item.setAttribute("role", "presentation");
      button = document.createElement("button");
      button.type = "button";
      button.id = `tab-button-${extension.id}`;
      button.className = "nav-link";
      button.setAttribute("role", "tab");
      button.setAttribute("aria-selected", "false");
      button.dataset.tabTarget = extension.id;
      button.textContent = I18n.message(extension.label);
      button.addEventListener("click", () => {
        selectTab(extension.id);
        void loadExtension(extension, byId(`tab-${extension.id}`));
      });
      item.append(button);
      tabs.append(item);
      pane = document.createElement("section");
      pane.id = `tab-${extension.id}`;
      pane.className = "tab-pane d-none";
      pane.setAttribute("role", "tabpanel");
      pane.setAttribute("aria-labelledby", button.id);
      byId("extension-panes").append(pane);
    }
    if (activeTab === extension.id) void loadExtension(extension, pane);
  }
  if (activeTab !== "overview" && !byId(`tab-${activeTab}`)) selectTab("overview");
}

/* Reorders the tab buttons; the first configured tab is opened once unless the user chose one. */
function applyTabOrder(order) {
  const ids = String(order || "").split(",").map((id) => id.trim()).filter((id) => byId(`tab-button-${id}`));
  const tabs = byId("web-tabs");
  for (const id of ids) tabs.append(byId(`tab-button-${id}`).parentElement);
  for (const button of [...tabs.querySelectorAll("[data-tab-target]")]) {
    if (!ids.includes(button.dataset.tabTarget)) tabs.append(button.parentElement);
  }
  if (!initialTabApplied && ids.length) {
    initialTabApplied = true;
    if (!userSelectedTab && ids[0] !== activeTab) {
      selectTab(ids[0]);
      const extension = currentStatus?.tabs?.find((tab) => tab.id === ids[0]);
      if (extension) void loadExtension(extension, byId(`tab-${extension.id}`));
    }
  }
}

document.getElementById("web-tabs").addEventListener("click", (event) => {
  if (event.target.closest("[data-tab-target]")) userSelectedTab = true;
});

document.querySelectorAll("[data-tab-target]").forEach((button) => {
  button.addEventListener("click", () => {
    selectTab(button.dataset.tabTarget);
    const extension = currentStatus?.tabs?.find((tab) => tab.id === button.dataset.tabTarget);
    if (extension) void loadExtension(extension, byId(`tab-${extension.id}`));
    if (button.dataset.tabTarget === "settings") {
      for (const placed of currentStatus?.tabs || []) {
        const pane = byId(`settings-ext-${placed.id}`);
        if (placed.placement === "settings" && pane) void loadExtension(placed, pane);
      }
    }
  });
});

function render(status) {
  csrfToken = status.csrfToken;
  currentStatus = status;
  document.title = status.project;
  byId("app-brand").textContent = status.project;
  addIcon(byId("app-brand"), "battery");
  primaryTabs = new Set(String(status.primaryTabs || "").split(",").filter(Boolean));
  renderExtensions(status.tabs);
  applyTabOrder(status.tabOrder);
  updateSections();
  for (const id of ["overview", "updates", "settings"]) decoratePane(byId(`tab-${id}`), id);
  if (status.activityUrl && !activityUrl && /^\/api\/[a-z0-9/-]+$/.test(status.activityUrl)) {
    activityUrl = status.activityUrl;
    byId("frame-indicator").classList.remove("d-none");
    void pollActivity();
  }
  byId("version").textContent = status.version;
  byId("project").textContent = status.project;
  byId("network").textContent = status.connected ? status.ssid : I18n.t("text.a0b9c89e98f8");
  byId("ip").textContent = status.ip || "-";
  byId("ap-ssid").textContent = status.apSsid;
  byId("time").textContent = status.timeReady ? I18n.t("text.da7f91ee2818") : I18n.t("text.b2c4157943a6");
  byId("network-message").textContent = I18n.message(status.networkMessage);
  showMessage("update-message", status.updateMessage, status.updateStale);
  byId("update-version").textContent = status.updateAvailable ?
    I18n.f("text.0c551374a2e7", {p0: status.availableVersion}) :
    (status.updateStale ? I18n.t("text.a33289b89647") : I18n.t("text.5a51af2d3d2f"));
  byId("update-notes").textContent = status.updateNotes || "";
  byId("update-last-success").textContent = status.updateLastSuccess ?
    I18n.f("text.4f76240d581d", {p0: new Date(status.updateLastSuccess * 1000).toLocaleString(I18n.locale)}) :
    (status.updateLastSuccessUptime ? I18n.f("text.2cd02542a0e5", {p0: status.updateLastSuccessUptime}) :
      I18n.t("text.f3418c09dace"));
  showMessage("ota-message", status.otaMessage, status.otaState === "failed");
  const transportWarning = byId("ota-transport-warning");
  transportWarning.textContent = status.otaInsecureHttp ?
    I18n.t("text.fe48d0b3cc1d") + " " +
    I18n.t("text.35c3527b6226") : "";
  transportWarning.className = status.otaInsecureHttp ?
    "alert alert-warning" : "alert alert-warning d-none";
  const requirements = [];
  if (!status.otaConfigured) requirements.push(I18n.t("text.df05dff19732"));
  if (!status.bootConfirmed) requirements.push(I18n.t("text.fcb95b66f76c"));
  if (!status.connected) requirements.push(I18n.t("text.335de1e0974a"));
  if (!status.timeReady && !status.otaInsecureHttp) {
    requirements.push(I18n.t("text.9d97003093ba"));
  }
  if (status.configuring) requirements.push(I18n.t("text.c86caa29de19"));
  byId("ota-requirement").textContent = requirements.join(" ");
  const progress = byId("ota-progress");
  if (status.otaBusy && status.totalBytes === 0) {
    progress.removeAttribute("value");
  } else {
    progress.value = status.totalBytes > 0 ?
      Math.min(100, Math.floor(100 * status.bytesRead / status.totalBytes)) : 0;
  }
  byId("ota-bytes").textContent = status.bytesRead > 0 ?
    I18n.f("text.8c71b63edb6f", {p0: status.bytesRead.toLocaleString(I18n.locale)}) +
    (status.totalBytes > 0 ? " " + I18n.f("text.32069b102eb3", {p0: status.totalBytes.toLocaleString(I18n.locale)}).trim() : "") :
    I18n.t("text.4af1783c7e64");
  updateButtons();
}

async function refresh() {
  const generation = statusGeneration;
  try {
    const response = await fetch("/api/status", {
      cache: "no-store",
      signal: AbortSignal.timeout(8000),
    });
    if (!response.ok) throw new Error(I18n.message(await response.text()));
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
        I18n.t("text.2fb8d503c411") :
        I18n.f("text.0a7df4bb8bc2", {p0: error.message}), !restarting);
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
    if (!response.ok) throw new Error(I18n.message(message));
    showMessage("action-result", message);
    currentStatus = null;
  } catch (error) {
    showMessage("action-result",
      I18n.f("text.344ad1023d39", {p0: error.message}), true);
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
    I18n.t("text.fc8923fe9d3b") : I18n.t("text.e9a0a82df908");
  if (confirm(I18n.f("text.65329196c5a1", {p0: version, p1: transport}) + " " +
              I18n.t("text.e8b52ff9b601"))) {
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
    showMessage("action-result", I18n.t("text.3dd1047a8924"), true);
    return;
  }
  byId("password").value = "";
  void sendAction("/api/network", { ssid, password });
});

void I18n.ready.then(refresh);
