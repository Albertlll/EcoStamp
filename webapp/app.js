const STORAGE_KEY = "ecolog_snapshots_v1";

const dom = {
  list: document.getElementById("snapshotList"),
  counter: document.getElementById("counter"),
  search: document.getElementById("searchInput"),
  add: document.getElementById("addSnapshot"),
  exportCsv: document.getElementById("exportCsv"),
  loadDevice: document.getElementById("loadDevice"),
  saveDevice: document.getElementById("saveDevice"),
  importCsv: document.getElementById("importCsv"),
  status: document.getElementById("statusBar"),
  form: document.getElementById("editForm"),
  deleteBtn: document.getElementById("deleteSnapshot"),
  tpl: document.getElementById("itemTemplate"),
  f: {
    id: document.getElementById("f_id"),
    timestamp: document.getElementById("f_timestamp"),
    lat: document.getElementById("f_lat"),
    lon: document.getElementById("f_lon"),
    temp_c: document.getElementById("f_temp_c"),
    fix_valid: document.getElementById("f_fix_valid"),
    temp_valid: document.getElementById("f_temp_valid"),
    captured_ms: document.getElementById("f_captured_ms"),
    note: document.getElementById("f_note"),
  },
};

let snapshots = loadSnapshots();
let selectedId = snapshots[0]?.id || null;
render();
setStatus("Локальные данные загружены");

function loadSnapshots() {
  const raw = localStorage.getItem(STORAGE_KEY);
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) return parsed;
    } catch (_) {}
  }
  return [
    {
      id: "A1B2C",
      timestamp: "20260302093010",
      lat: 55.7512,
      lon: 37.6184,
      temp_c: 12.4,
      fix_valid: 1,
      temp_valid: 1,
      captured_ms: 120301,
      note: "Демо запись",
    },
  ];
}

function saveSnapshots() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(snapshots));
}

function setStatus(text, isError = false) {
  dom.status.textContent = text;
  dom.status.style.background = isError ? "#fdecec" : "#eaf3ff";
  dom.status.style.borderColor = isError ? "#f5c2c0" : "#cadcf8";
  dom.status.style.color = isError ? "#7a1620" : "#1f3a65";
}

function generateId() {
  const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  let id = "";
  for (let i = 0; i < 5; i++) id += chars[Math.floor(Math.random() * chars.length)];
  return id;
}

function render() {
  const q = dom.search.value.trim().toLowerCase();
  const filtered = snapshots.filter((s) => {
    const text = `${s.id} ${s.timestamp} ${s.lat} ${s.lon} ${s.note || ""}`.toLowerCase();
    return text.includes(q);
  });

  dom.list.innerHTML = "";
  filtered.forEach((s) => {
    const node = dom.tpl.content.firstElementChild.cloneNode(true);
    node.dataset.id = s.id;
    node.querySelector(".item-id").textContent = s.id;
    node.querySelector(".item-time").textContent = s.timestamp;
    node.querySelector(".item-meta").textContent = `T=${fmt(s.temp_c)}°C | ${fmt(s.lat, 5)}, ${fmt(s.lon, 5)}`;
    if (s.id === selectedId) node.classList.add("active");
    node.addEventListener("click", () => {
      selectedId = s.id;
      fillForm(s);
      render();
    });
    dom.list.appendChild(node);
  });

  dom.counter.textContent = `${filtered.length} из ${snapshots.length}`;

  const selected = snapshots.find((s) => s.id === selectedId) || filtered[0] || snapshots[0];
  if (selected) {
    selectedId = selected.id;
    fillForm(selected);
  } else {
    dom.form.reset();
  }
}

function fillForm(s) {
  dom.f.id.value = s.id || "";
  dom.f.timestamp.value = s.timestamp || "";
  dom.f.lat.value = s.lat ?? "";
  dom.f.lon.value = s.lon ?? "";
  dom.f.temp_c.value = s.temp_c ?? "";
  dom.f.fix_valid.checked = !!Number(s.fix_valid);
  dom.f.temp_valid.checked = !!Number(s.temp_valid);
  dom.f.captured_ms.value = s.captured_ms ?? "";
  dom.f.note.value = s.note || "";
}

function fmt(v, d = 1) {
  const n = Number(v);
  return Number.isFinite(n) ? n.toFixed(d) : "-";
}

function parseCsv(text) {
  const lines = text.trim().split(/\r?\n/).filter(Boolean);
  if (!lines.length) return [];
  const headers = lines[0].split(",").map((h) => h.trim());
  return lines.slice(1).map((line) => {
    const cols = line.split(",");
    const obj = {};
    headers.forEach((h, i) => (obj[h] = (cols[i] ?? "").trim()));
    obj.lat = Number(obj.lat);
    obj.lon = Number(obj.lon);
    obj.temp_c = Number(obj.temp_c);
    obj.fix_valid = Number(obj.fix_valid);
    obj.temp_valid = Number(obj.temp_valid);
    obj.captured_ms = Number(obj.captured_ms);
    obj.note = obj.note || "";
    return obj;
  });
}

function toCsv(rows) {
  const headers = ["id", "timestamp", "lat", "lon", "temp_c", "fix_valid", "temp_valid", "captured_ms", "note"];
  const body = rows.map((s) => headers.map((h) => String(s[h] ?? "")).join(","));
  return [headers.join(","), ...body].join("\n");
}

function download(name, content) {
  const blob = new Blob([content], { type: "text/csv;charset=utf-8" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  URL.revokeObjectURL(a.href);
}

dom.search.addEventListener("input", render);

dom.add.addEventListener("click", () => {
  const now = new Date();
  const ts = `${now.getFullYear()}${String(now.getMonth() + 1).padStart(2, "0")}${String(now.getDate()).padStart(2, "0")}${String(now.getHours()).padStart(2, "0")}${String(now.getMinutes()).padStart(2, "0")}${String(now.getSeconds()).padStart(2, "0")}`;
  const row = {
    id: generateId(),
    timestamp: ts,
    lat: 0,
    lon: 0,
    temp_c: 0,
    fix_valid: 0,
    temp_valid: 0,
    captured_ms: Date.now(),
    note: "",
  };
  snapshots.unshift(row);
  selectedId = row.id;
  saveSnapshots();
  render();
});

dom.form.addEventListener("submit", (e) => {
  e.preventDefault();
  const idx = snapshots.findIndex((s) => s.id === selectedId);
  if (idx < 0) return;

  const nextId = dom.f.id.value.trim().toUpperCase();
  snapshots[idx] = {
    ...snapshots[idx],
    id: nextId,
    timestamp: dom.f.timestamp.value.trim(),
    lat: Number(dom.f.lat.value || 0),
    lon: Number(dom.f.lon.value || 0),
    temp_c: Number(dom.f.temp_c.value || 0),
    fix_valid: dom.f.fix_valid.checked ? 1 : 0,
    temp_valid: dom.f.temp_valid.checked ? 1 : 0,
    captured_ms: Number(dom.f.captured_ms.value || 0),
    note: dom.f.note.value.trim(),
  };
  selectedId = nextId;
  saveSnapshots();
  render();
});

dom.deleteBtn.addEventListener("click", () => {
  if (!selectedId) return;
  snapshots = snapshots.filter((s) => s.id !== selectedId);
  selectedId = snapshots[0]?.id || null;
  saveSnapshots();
  render();
});

dom.exportCsv.addEventListener("click", () => {
  download("samples_export.csv", toCsv(snapshots));
});

dom.loadDevice.addEventListener("click", async () => {
  try {
    const res = await fetch("/samples.csv", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const csv = await res.text();
    const rows = parseCsv(csv);
    snapshots = rows;
    selectedId = snapshots[0]?.id || null;
    saveSnapshots();
    render();
    setStatus(`Загружено с устройства: ${rows.length} записей`);
  } catch (e) {
    setStatus(`Ошибка загрузки с устройства: ${e.message}`, true);
  }
});

dom.saveDevice.addEventListener("click", async () => {
  try {
    const csv = toCsv(snapshots);
    const res = await fetch("/samples.csv", {
      method: "POST",
      headers: { "Content-Type": "text/csv; charset=utf-8" },
      body: csv,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    setStatus(`Сохранено на устройство: ${snapshots.length} записей`);
  } catch (e) {
    setStatus(`Ошибка сохранения на устройство: ${e.message}`, true);
  }
});

dom.importCsv.addEventListener("change", async (e) => {
  const file = e.target.files?.[0];
  if (!file) return;
  const txt = await file.text();
  const rows = parseCsv(txt);
  if (rows.length) {
    snapshots = rows;
    selectedId = snapshots[0].id;
    saveSnapshots();
    render();
    setStatus(`Импортировано из CSV: ${rows.length} записей`);
  }
  dom.importCsv.value = "";
});
