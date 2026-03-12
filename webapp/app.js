const STORAGE_KEY = "ecolog_device_url";

const dom = {
  deviceUrl: document.getElementById("deviceUrl"),
  connectBtn: document.getElementById("connectBtn"),
  refreshBtn: document.getElementById("refreshBtn"),
  status: document.getElementById("status"),
  recordsCount: document.getElementById("recordsCount"),
  latestRecord: document.getElementById("latestRecord"),
  searchInput: document.getElementById("searchInput"),
  recordsList: document.getElementById("recordsList"),
  recordTemplate: document.getElementById("recordTemplate"),
};

let records = [];

bootstrap();

function bootstrap() {
  const savedUrl = localStorage.getItem(STORAGE_KEY);
  const defaultUrl = window.location.protocol.startsWith("http")
    ? window.location.origin
    : "http://192.168.4.1";

  dom.deviceUrl.value = savedUrl || defaultUrl;
  bindEvents();

  if (window.location.origin !== "null") {
    void connectAndLoad();
  }
}

function bindEvents() {
  dom.connectBtn.addEventListener("click", async () => {
    await checkStatus();
  });

  dom.refreshBtn.addEventListener("click", async () => {
    await connectAndLoad();
  });

  dom.searchInput.addEventListener("input", renderRecords);
}

function normalizeBaseUrl() {
  const raw = dom.deviceUrl.value.trim().replace(/\/+$/, "");
  const withProto = /^https?:\/\//i.test(raw) ? raw : `http://${raw}`;
  localStorage.setItem(STORAGE_KEY, withProto);
  dom.deviceUrl.value = withProto;
  return withProto;
}

function setStatus(text, type = "info") {
  dom.status.textContent = text;
  dom.status.dataset.type = type;
}

async function fetchJson(path) {
  const baseUrl = normalizeBaseUrl();
  const response = await fetch(`${baseUrl}${path}`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

async function checkStatus() {
  try {
    setStatus("Проверка связи с устройством...", "info");
    const status = await fetchJson("/api/status");
    const sdText = status.sd_ready ? "SD готова" : "SD недоступна";
    setStatus(`Подключено к ${status.ssid} (${status.ip}), ${sdText}`, "ok");
    return status;
  } catch (error) {
    setStatus(`Нет связи с ESP: ${error.message}`, "error");
    throw error;
  }
}

async function connectAndLoad() {
  try {
    await checkStatus();
    setStatus("Чтение записей с SD-карты...", "info");
    const payload = await fetchJson("/api/records");
    records = Array.isArray(payload.records) ? payload.records : [];
    updateMetrics();
    renderRecords();
    setStatus(`Загружено ${records.length} записей`, "ok");
  } catch (error) {
    records = [];
    updateMetrics();
    renderRecords();
    setStatus(`Ошибка загрузки: ${error.message}`, "error");
  }
}

function updateMetrics() {
  dom.recordsCount.textContent = String(records.length);
  dom.latestRecord.textContent = records[0]
    ? `${records[0].id} · ${formatTimestamp(records[0].timestamp)}`
    : "-";
}

function renderRecords() {
  const query = dom.searchInput.value.trim().toLowerCase();
  const filtered = records.filter((record) => {
    const haystack = [
      record.id,
      record.timestamp,
      record.note,
      record.lat,
      record.lon,
      record.temp_c,
    ]
      .join(" ")
      .toLowerCase();
    return haystack.includes(query);
  });

  dom.recordsList.innerHTML = "";
  if (!filtered.length) {
    dom.recordsList.innerHTML = '<div class="empty-state">Записи не найдены</div>';
    return;
  }

  filtered.forEach((record) => {
    const card = dom.recordTemplate.content.firstElementChild.cloneNode(true);
    card.querySelector(".record-id").textContent = record.id || "-";
    card.querySelector(".record-time").textContent = formatTimestamp(record.timestamp);
    card.querySelector(".record-coords").textContent = formatCoords(record.lat, record.lon);
    card.querySelector(".record-temp").textContent = Number(record.temp_valid)
      ? `${formatNumber(record.temp_c, 1)} °C`
      : "нет данных";
    card.querySelector(".record-fix").textContent = Number(record.fix_valid) ? "валиден" : "нет";
    card.querySelector(".record-ms").textContent = String(record.captured_ms ?? 0);
    card.querySelector(".record-note").textContent = record.note || "Без заметки";
    dom.recordsList.appendChild(card);
  });
}

function formatCoords(lat, lon) {
  if (!Number.isFinite(Number(lat)) || !Number.isFinite(Number(lon))) {
    return "нет данных";
  }
  return `${formatNumber(lat, 5)}, ${formatNumber(lon, 5)}`;
}

function formatNumber(value, digits) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(digits) : "-";
}

function formatTimestamp(timestamp) {
  const value = String(timestamp || "");
  if (!/^\d{14}$/.test(value)) {
    return value || "-";
  }
  return `${value.slice(6, 8)}.${value.slice(4, 6)}.${value.slice(0, 4)} ${value.slice(8, 10)}:${value.slice(10, 12)}:${value.slice(12, 14)}`;
}
