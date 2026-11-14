"""Flask app of the drum editor.  python3 tools/drums/drum_gui.py  ->  http://127.0.0.1:5001

API (JSON unless noted):
  GET  /api/meta                 instrument names, library, project list, device support
  POST /api/validate             {doc} -> {doc} normalised, or {error}
  POST /api/render               {doc, mode: pattern|song|hit|kit, pat, bars, inst, vel} -> audio/wav (header X-Render: peak/secs/clipped)
  POST /api/image                {doc} -> the 4247-byte machine image (application/octet-stream)
  POST /api/open_image           raw bytes -> {doc}
  POST /api/library/<kind>       {entry} save into the user's library   DELETE /api/library/<kind>?name=&inst=
  GET/POST /api/project          list / save,  GET /api/project/<name>
  GET  /api/device/ports   POST /api/device/fetch {port}   POST /api/device/send {port, doc, flash}   POST /api/device/hit {port, inst, vel}"""
import os, tempfile
from flask import Flask, jsonify, request, send_file, send_from_directory, Response
from . import core, device
from .core import DrumError
from .engine import Engine

def create_app(cache=None):
    app = Flask(__name__, static_folder=os.path.join(os.path.dirname(os.path.abspath(__file__)), "static"), static_url_path="/static")
    eng = Engine(cache); links = {}
    work = tempfile.mkdtemp(prefix="drumconf_")

    @app.errorhandler(Exception)
    def any_error(e):
        from werkzeug.exceptions import HTTPException
        if isinstance(e, DrumError): return jsonify(error=str(e)), 400
        if isinstance(e, HTTPException): return jsonify(error="%s %s: %s" % (e.code, e.name, e.description)), e.code
        app.logger.exception("unexpected error")
        return jsonify(error="%s: %s" % (type(e).__name__, e)), 500

    def body(): return request.get_json(silent=True) or {}
    @app.route("/")
    def index(): return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/meta")
    def meta():
        try: has_serial = bool(device._serial())
        except DrumError: has_serial = False
        return jsonify(inst=core.INST, inst_name=core.INST_NAME, fields=core.KIT_FIELDS, library=core.library(), projects=core.list_projects(), serial=has_serial,
                       default_kit=core.DEFAULT_KIT, image_size=core.SAVE_SIZE)

    @app.post("/api/validate")
    def validate(): return jsonify(doc=core.validate(body().get("doc") or {}))

    @app.post("/api/render")
    def render():
        j = body(); doc = core.validate(j.get("doc") or {}); mode = j.get("mode", "pattern")
        if mode == "pattern": args = ["pattern", int(j.get("pat", doc["selPat"])), int(j.get("bars", 2))] + (["loop"] if j.get("loop") else [])
        elif mode == "song": args = ["song"]
        elif mode == "hit": args = ["hit", int(j.get("inst", 0)), int(j.get("vel", 100))]
        elif mode == "kit": args = ["kit", int(j.get("vel", 100))]
        else: raise DrumError("unknown render mode %r" % mode)
        out = os.path.join(work, "out.wav"); info = eng.render(doc, args, out)
        with open(out, "rb") as f: data = f.read()
        r = Response(data, mimetype="audio/wav"); r.headers["X-Render"] = "peak=%d;secs=%.2f;clipped=%d" % (info["peak"], info["secs"], info["clipped"])
        r.headers["Access-Control-Expose-Headers"] = "X-Render"; r.headers["Cache-Control"] = "no-store"
        return r

    @app.post("/api/image")
    def image():
        b = core.to_image(body().get("doc") or {})
        r = Response(b, mimetype="application/octet-stream"); r.headers["Content-Disposition"] = "attachment; filename=drums.bin"; return r

    @app.post("/api/open_image")
    def open_image(): return jsonify(doc=core.from_image(request.get_data(), name=request.args.get("name", "from file")))

    @app.post("/api/library/<kind>")
    def lib_save(kind): return jsonify(entry=core.save_user_entry(kind, body().get("entry") or {}), library=core.library())

    @app.delete("/api/library/<kind>")
    def lib_delete(kind):
        core.delete_user_entry(kind, request.args.get("name", ""), request.args.get("inst")); return jsonify(library=core.library())

    @app.get("/api/project")
    def project_list(): return jsonify(projects=core.list_projects())
    @app.post("/api/project")
    def project_save(): d = core.save_project(body().get("doc") or {}); return jsonify(doc=d, projects=core.list_projects())
    @app.get("/api/project/<name>")
    def project_load(name): return jsonify(doc=core.load_project(name))

    @app.get("/api/device/ports")
    def ports(): return jsonify(ports=device.list_ports())

    def link(port):
        if not port: raise DrumError("choose the serial port of the machine first")
        l = links.get(port)
        if l is None: l = links[port] = device.Link(port)
        return l
    def drop(port):
        l = links.pop(port, None)
        if l: l.close()

    @app.post("/api/device/fetch")
    def dev_fetch():
        j = body()
        try: return jsonify(doc=link(j.get("port")).fetch())
        except DrumError: drop(j.get("port")); raise
    @app.post("/api/device/send")
    def dev_send():
        j = body()
        try: link(j.get("port")).send(j.get("doc") or {}, flash=bool(j.get("flash"))); return jsonify(ok=True)
        except DrumError: drop(j.get("port")); raise
    @app.post("/api/device/hit")
    def dev_hit():
        j = body(); link(j.get("port")).hit(int(j.get("inst", 0)), int(j.get("vel", 100))); return jsonify(ok=True)
    @app.post("/api/device/play")
    def dev_play():
        j = body(); link(j.get("port")).play(bool(j.get("on", True))); return jsonify(ok=True)
    @app.post("/api/device/close")
    def dev_close(): drop(body().get("port")); return jsonify(ok=True)
    return app
