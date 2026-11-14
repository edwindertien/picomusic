"""A small SFZ reader: enough regions/opcodes to import the free Rhodes / piano / clavinet sets (<global> <master> <group> <region>, default_path, #define)."""
import os, re
def _tokens(text):
    text = re.sub(r"//[^\n]*", "", text)
    defs = dict(re.findall(r"#define\s+(\$\w+)\s+(\S+)", text)); text = re.sub(r"#define[^\n]*", "", text)
    for k, v in defs.items(): text = text.replace(k, v)
    return text
def parse(path):
    base = os.path.dirname(os.path.abspath(path)); text = _tokens(open(path, encoding='utf-8', errors='replace').read())
    parts = re.split(r"(<\w+>)", text); scopes = {"global": {}, "master": {}, "group": {}}; regions = []; cur = None; control = {}
    for p in parts:
        if re.fullmatch(r"<\w+>", p): cur = p[1:-1]; 
        else:
            if cur is None: continue
            # opcodes: name=value, value may contain spaces only for sample= paths
            ops = {}
            for m in re.finditer(r"(\w+)=((?:(?!\s+\w+=).)*)", p.strip(), re.S): ops[m.group(1)] = m.group(2).strip()
            if cur == "control": control.update(ops)
            elif cur in scopes:
                scopes[cur] = ops
                if cur == "global": scopes["master"] = {}; scopes["group"] = {}
                if cur == "master": scopes["group"] = {}
            elif cur == "region":
                r = {}
                for sc in ("global", "master", "group"): r.update(scopes[sc])
                r.update(ops); regions.append(r)
    out = []; dp = control.get("default_path", "").replace("\\", "/")
    for r in regions:
        if "sample" not in r or r.get("trigger", "attack") != "attack": continue
        def num(k, d=None):
            v = r.get(k); 
            if v is None: return d
            try: return int(float(v))
            except ValueError: return None
        from .notes import parse_note
        def key(k, d):
            v = r.get(k); return d if v is None else parse_note(v)
        kc = key("pitch_keycenter", key("key", None)); lo = key("lokey", key("key", 0)); hi = key("hikey", key("key", 127))
        if kc is None: kc = lo
        f = os.path.normpath(os.path.join(base, dp, r["sample"].replace("\\", "/")))
        ls, le = num("loop_start"), num("loop_end"); mode = r.get("loop_mode", "no_loop" if ls is None else "loop_continuous")
        out.append(dict(file=f, root=kc, lo=lo, hi=hi, vlo=max(1, num("lovel", 0) or 0), vhi=num("hivel", 127), tune=num("tune", 0) or 0, volume=float(r.get("volume", 0) or 0),
                        loop_sfz=(ls, le) if (mode in ("loop_continuous", "loop_sustain") and ls is not None and le is not None) else None))
    return out
