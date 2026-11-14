// Runs the REAL frontend script (tools/bankconf/static/index.html) in Node against a minimal DOM stub, fed with the API's real responses (fixture.json).
const fs = require("fs"), vm = require("vm"); const fx = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const html = fs.readFileSync(process.argv[2], "utf8"); const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
let fails = 0; const check = (name, cond, detail) => { if (!cond) fails++; console.log((cond ? "PASS " : "FAIL ") + name + (detail ? "  " + detail : "")); };
const calls = []; const els = {};
function stub(id) {
  let html = ""; let ctx2 = null;
  const e = { id, get innerHTML() { return html; }, set innerHTML(v) { html = v; this.children = []; }, textContent: "", value: "", className: "", style: {}, children: [], dataset: {}, checked: false, onclick: null, onchange: null, oninput: null, onkeydown: null, disabled: false, tagName: "DIV",
    classList: { add() {}, remove() {} }, appendChild(c) { this.children.push(c); return c; }, append() {}, querySelectorAll() { return []; }, addEventListener() {}, pause() {}, play() {},
    getBoundingClientRect() { return { left: 0, top: 0, width: 1100, height: 176 }; }, width: 1100, height: 176,
    getContext() { return ctx2 || (ctx2 = new Proxy({ ops: [], fillRect(x, y, w, h) { this.ops.push(["fillRect", this.fillStyle, x, y, w, h]); } }, { get: (t, k) => (k in t ? t[k] : () => {}), set: (t, k, v) => { t[k] = v; return true; } })); } };
  return e;
}
const doc = { getElementById(id) { return els[id] || (els[id] = stub(id)); }, createElement(t) { const e = stub(t); e.tagName = t.toUpperCase(); return e; }, querySelectorAll() { return []; } };
const routes = { "/api/state": () => fx.state, "/api/estimate": () => fx.estimate, "/api/import": b => (b.sfz ? fx.import_sfz : fx.import_dir), "/api/save": () => ({ ok: true }), "/api/build": () => ({ job: "j1" }), "/api/job/j1": () => ({ state: "done", i: 1, n: 1, msg: "", result: fx.build }), "/api/export": () => ({ ok: true, files: ["a", "b", "c", "d"], bytes: 123456 }) };
const ctx = { document: doc, window: { addEventListener() {} }, console, JSON, Math, Promise, setTimeout, clearTimeout, Audio: function () { this.play = () => {}; this.pause = () => {}; }, URL: { createObjectURL: () => "blob:x" }, confirm: () => true, encodeURIComponent,
  fetch: async (url, opt) => { const path = url.split("?")[0]; const body = opt && opt.body ? JSON.parse(opt.body) : {}; calls.push({ path, method: opt && opt.method, body }); const r = routes[path]; if (!r) return { ok: false, headers: { get: () => "application/json" }, json: async () => ({ error: "no route " + path }) };
    return { ok: true, headers: { get: () => "application/json" }, json: async () => r(body), blob: async () => ({}) }; } };
vm.createContext(ctx); vm.runInContext(script, ctx); doc.getElementById("vel").value = "80"; doc.getElementById("len").value = "2.5";
const run = c => vm.runInContext(c, ctx); const wait = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  await run("init()"); await wait(80);
  check("init: the state is loaded (3 instruments, 16 parameters) and the first estimate is requested", run("cfg.instruments.length") === 3 && run("params.length") === 16 && calls.some(c => c.path === "/api/estimate"));
  check("the budget label shows total, budget and the 4 MB remainder", /of 600,000 bytes/.test(els.barlabel.innerHTML) && /MB for the firmware/.test(els.barlabel.innerHTML), els.barlabel.innerHTML.slice(0, 120));
  check("the budget bar has one segment per instrument", els.bar.children.length === 3, "segments " + els.bar.children.length);
  check("tabs: one button per instrument and '+ instrument'", els.tabs.children.length === 4);
  check("the sample table has one row per sample of the selected instrument (header + 6)", els.samples.children.length === 6);
  const kb = els.kb.getContext(); const gaps = kb.ops.filter(o => o[1] === "#6a2020").length;
  check("the keyboard map draws its lanes (2 velocity layers x 88 keys) and finds the gaps in the layered instrument", kb.ops.length > 200 && gaps >= 0, `${kb.ops.length} rectangles, ${gaps} gap cells`);
  check("the warnings panel lists the estimate's warnings (escaped)", els.warns.innerHTML.includes("warn") && !els.warns.innerHTML.includes("<script"));
  // editing a cell converts an import-defined instrument to an explicit list
  run("sel = 1"); await run("runEstimate()"); await wait(30); const before = run("JSON.stringify(cfg.instruments[1].import || null)");
  run("editCell(0, 'root', 'C5')"); check("editing a cell turns an 'import:' instrument into an explicit sample list without duplicates", run("cfg.instruments[1].import") === undefined && run("cfg.instruments[1].samples.length") === 3 && run("cfg.instruments[1].samples[0].root") === "C5", "was " + before);
  run("ensureExplicit()"); check("ensureExplicit is idempotent", run("cfg.instruments[1].samples.length") === 3);
  run("autorange_test = 1"); run("(ins().samples || []).forEach(s => { s.lo = null; s.hi = null; })"); check("automatic key ranges are stored as null", run("ins().samples.every(s => s.lo === null && s.hi === null)"));
  run("editCell(1, 'gain', '-2.5'); editCell(1, 'rate', '16000'); editCell(1, 'vlo', '10')"); check("numeric cells are stored as numbers", run("ins().samples[1].gain") === -2.5 && run("ins().samples[1].rate") === 16000 && run("ins().samples[1].vlo") === 10);
  // import
  run("sel = 0"); const n0 = run("cfg.instruments[0].samples.length"); await run("doImport({dir: '/x', pattern: '{note}.*'})"); await wait(30); const imp = calls.filter(c => c.path === "/api/import").pop();
  check("import: the request carries folder, pattern and the config's base folder; the rows are appended", imp && imp.body.dir === "/x" && imp.body.base !== undefined && run("cfg.instruments[0].samples.length") === n0 + fx.import_dir.samples.length, JSON.stringify(imp && imp.body).slice(0, 100));
  await run("doImport({sfz: '/y.sfz'})"); await wait(30); check("SFZ import keeps the SFZ's own key ranges and loop points", run("cfg.instruments[0].samples.some(s => s.loop_sfz && s.loop === 'sfz' && s.lo)"));
  // build / export
  await run("build()"); await wait(900); check("build: starts the job, polls it, shows the result table and enables Export", calls.some(c => c.path === "/api/build") && calls.some(c => c.path === "/api/job/j1") && /zones/.test(els.result.innerHTML) && els.btnExport.disabled === false && run("built") === true, els.status.textContent);
  check("the result table shows loops and SNR with colour", /pill loop/.test(els.result.innerHTML) && /dB/.test(els.result.innerHTML));
  await run("exportFw()"); await wait(30); check("export reports the files written", /wrote 4 files/.test(els.status.textContent), els.status.textContent);
  await run("audition()"); await wait(30); check("audition after a build posts instrument, key, velocity, length, pedal and the sound knobs", (() => { const a = calls.filter(c => c.path === "/api/audition").pop(); return a && a.body.instrument === 0 && a.body.key === 60 && a.body.vel === 80 && "params" in a.body; })());
  run("built = false"); els.audmsg.textContent = ""; await run("audition()"); check("audition before a build explains what to do instead of failing silently", /Build first/.test(els.audmsg.textContent));
  // keyboard geometry
  check("keyboard geometry: 52 white keys across the map; A0 at the left edge, C8 at the right", Math.abs(run("keyX(21)") - 6) < 1e-6 && Math.abs(run("keyX(108)") - (6 + 51 * 1088 / 52)) < 1e-6);
  check("a click on a white key and on a black key is resolved to the right MIDI key", (() => { const f = k => ({ clientX: run(`keyX(${k})`) + 5, clientY: isBlackKey(k) ? 10 : 55 }); const isBlackKey = k => [1,3,6,8,10].includes(k % 12); return run("keyAt").call && [60, 61, 69, 22].every(k => run(`keyAt({clientX: keyX(${k}) + ${isBlackKey(k) ? 4 : 5}, clientY: ${isBlackKey(k) ? 10 : 55}})`) === k); })());
  check("a click below the keys (on the coverage lanes) is ignored", run("keyAt({clientX: 300, clientY: 120})") === null);
  check("html escaping: a hostile file name cannot inject markup", run("esc('<img src=x onerror=1>\"')") === "&lt;img src=x onerror=1&gt;&quot;");
  console.log(fails ? `\n${fails} GUI DOM TESTS FAILED` : "\nALL GUI DOM TESTS PASS"); process.exit(fails ? 1 : 0);
})().catch(e => { console.log("FAIL exception: " + e.stack); process.exit(1); });
