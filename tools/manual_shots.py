"""Screenshots for the user manual: python tools/manual.py shots [NAME...]

Each docs/manual/shots/<name>.json says how one picture is made:

    {
      "config": {"GENERAL": {"machine": "ZX48K"}},   settings on top of the factory ones;
                                                     {media} in a value as in "args"
      "args": ["-s", "2"],                           command line of the program;
                                                     {media} is the folder of shots/make_media.py
      "files": {"GAME.TAP": {"url": "...", "sha256": "..."}}
                                                     put into {media} too: what the repository
                                                     may not carry, as a game, downloaded once
                                                     into build/manual-files (from a .zip, the
                                                     file of that name in it, or "member")
      "prepare": ["{media}/GAME.TAP", "--bench", "2400", "--bench-save", "{media}/game.sna"]
                                                     a run with no window before the shot, as
                                                     to get into a game for a snapshot
      "steps": [["wait", 3000], ["run", "run-action key.tapewin"]],
      "frames": [{"n": 1, "widget": "QMenuBar"}],    numbered frames around widgets
      "crop": {"widget": "QToolBar", "pad": 6}       or [x, y, w, h]; none = the whole window
      "zoom": 2, "badges": "top"                     whole-pixel zoom; numbers above the frames
                                                     ("inside": in their top left corner)
      "capture": "menus"                             the window with the menus open-menu opened;
                                                     "crop": {"menus": true, "top": true} then
    }

A step is ["wait", ms], ["run", "<control port command>"], or ["click", "<widget>",
dx, dy] - a click at that point of a widget of the window in front (a page of
Options, a tab); "click-nowait" for one that opens a menu. The picture is the
window "winshot" takes when the steps are done: a menu or dialog open over the
main window, a tool window, or the main window. A frame is {"n", "widget"} or
{"n", "rect": [x, y, w, h]}, in the picture's own coordinates; "widget" is an
object name or a class name, as the control port's widget-rect takes it.

The program runs with a fresh copy of the factory settings, on a desktop of its
own on Windows (nothing shows on yours), and is stopped when the picture is
taken. XP_EXE picks the program; by default the newest build\\dist\\*-qt6.
Needs Pillow.
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
def free_port():
    # one nothing listens on, so no other copy of the program - another session's,
    # or one of yours - takes the commands; a shot can set its own
    # ("config": {"GENERAL": {"port": ...}})
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
ACCENT = (230, 60, 110) # frames and badges: reads on the dark picture and the light bars
DESKTOP = "xpeccy_manual_shots"
SEP = "\\" if os.name == "nt" else "/"
NEUTRAL_HOME = "C:\\Users\\Player\\" if os.name == "nt" else "/home/player/"


def find_exe():
    env = os.environ.get("XP_EXE")
    if env:
        return Path(env)
    dists = sorted((ROOT / "build" / "dist").glob("*-qt6"), key=lambda p: p.stat().st_mtime)
    if not dists:
        sys.exit("no build in build/dist: build one, or set XP_EXE")
    return dists[-1] / ("xpeccy-plus.exe" if os.name == "nt" else "xpeccy-plus")


def write_config(cfg, extra):
    # The settings a first start writes (res/fallback/config.conf), with a few
    # keys changed: no first-start question, a port of its own, and the paths a
    # page shows - someone's own home folder must not end up in the manual. No sound
    # out: a game playing on a hidden desktop is heard all the same.
    port = str(extra.get("GENERAL", {}).get("port") or free_port())
    sets = {"GENERAL": {"start": "last", "port": port},
        "SOUND": {"volume.master": "0"},
        "VIDEO": {"scrDir": NEUTRAL_HOME + "Pictures", "rec.dir": NEUTRAL_HOME + "Videos",
            "rec.ffmpeg": NEUTRAL_HOME + "FFmpeg" + SEP + "ffmpeg" + (".exe" if os.name == "nt" else "")}}
    for sec, keys in extra.items():
        sets.setdefault(sec, {}).update(keys)
    out, sec = [], None

    def rest():
        # what is left of a section goes at its end
        for k, v in sets.pop(sec, {}).items():
            out.append(f"{k} = {v}")

    for ln in (ROOT / "res" / "fallback" / "config.conf").read_text(encoding="utf-8").splitlines():
        s = ln.strip()
        if s.startswith("[") and s.endswith("]"):
            rest()
            sec = s[1:-1]
        elif "=" in s and not s.startswith("#"):
            k = s.split("=", 1)[0].strip()
            if k in sets.get(sec, {}):
                ln = f"{k} = {sets[sec].pop(k)}"
        out.append(ln)
    rest()
    for sec_name, keys in sets.items():
        out += ["", f"[{sec_name}]", ""] + [f"{k} = {v}" for k, v in keys.items()]
    (cfg / "config.conf").write_text("\n".join(out) + "\n", encoding="utf-8")
    return int(port)


def start_hidden(cmd, cwd):
    # CreateProcess on a desktop of its own: the windows exist and paint, unseen
    import ctypes
    from ctypes import wintypes
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    user32.CreateDesktopW.restype = wintypes.HANDLE
    if not user32.CreateDesktopW(DESKTOP, None, None, 0, 0x10000000, None):
        sys.exit("cannot create a desktop for the shots")

    class SI(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("r", wintypes.LPWSTR), ("desk", wintypes.LPWSTR),
            ("title", wintypes.LPWSTR), ("x", wintypes.DWORD), ("y", wintypes.DWORD),
            ("xs", wintypes.DWORD), ("ys", wintypes.DWORD), ("xc", wintypes.DWORD),
            ("yc", wintypes.DWORD), ("fill", wintypes.DWORD), ("flags", wintypes.DWORD),
            ("show", wintypes.WORD), ("r2", wintypes.WORD), ("r3", ctypes.c_void_p),
            ("i", wintypes.HANDLE), ("o", wintypes.HANDLE), ("e", wintypes.HANDLE)]

    class PI(ctypes.Structure):
        _fields_ = [("hp", wintypes.HANDLE), ("ht", wintypes.HANDLE),
            ("pid", wintypes.DWORD), ("tid", wintypes.DWORD)]

    si = SI(cb=ctypes.sizeof(SI), desk=DESKTOP)
    pi = PI()
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(c) for c in cmd]))
    if not kernel32.CreateProcessW(None, line, None, None, False, 0, None, str(cwd),
            ctypes.byref(si), ctypes.byref(pi)):
        sys.exit("cannot start the program")
    kernel32.CloseHandle(pi.ht)
    return pi.pid, pi.hp


def stop(pid, handle):
    if handle is not None:
        import ctypes
        ctypes.windll.kernel32.TerminateProcess(handle, 0)
        ctypes.windll.kernel32.CloseHandle(handle)
    else:
        pid.kill()


class Port:
    def __init__(self, num):
        self.num = num
        self.held = []      # connections waiting for an answer that comes late
        self.connect()

    def connect(self):
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.num), timeout=2)
                break
            except OSError:
                time.sleep(0.2)
        else:
            sys.exit(f"the program does not answer on port {self.num}")
        self.read()

    def read(self, wait=10.0):
        # every answer ends with the "> " prompt
        data = b""
        self.sock.settimeout(wait)
        while not data.endswith(b"> "):
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", "replace")[:-2].strip()

    def run(self, command):
        self.sock.sendall(command.encode("utf-8") + b"\n")
        return self.read()


def click_in(port, widget, dx, dy, tmp, wait=True, button=""):
    command = f"click-widget {widget} {dx} {dy} {button}".strip()
    if wait:
        return port.run(command)
    # A click that opens a menu answers only when the menu closes, and while it is
    # open this connection gets nothing more: the next commands go on a new one
    port.sock.sendall(command.encode("utf-8") + b"\n")
    port.held.append(port.sock)
    port.connect()
    return "sent"


def union(rects):
    x0 = min(r[0] for r in rects)
    y0 = min(r[1] for r in rects)
    return [x0, y0, max(r[0] + r[2] for r in rects) - x0, max(r[1] + r[3] for r in rects) - y0]


def rect_of(port, widget):
    ans = port.run(f"widget-rect {widget}")
    try:
        return [int(v) for v in ans.split()[:4]]
    except ValueError:
        sys.exit(f"widget-rect {widget}: {ans}")


BADGE = 11          # radius of a number's circle


def annotate(img, frames, crop, zoom=1, top=False, inside=False):
    """Crop, zoom, then frame the widgets; the numbers go in a clear margin - on the
    left, or above for a strip such as the status bar - each joined to its frame, so
    they cover nothing of the window. Inside: in the frame's top left corner, for
    panels side by side whose title bars have room there."""
    from PIL import Image, ImageDraw, ImageFont
    ox = oy = 0
    if crop:
        x, y, w, h = crop
        box = (max(x, 0), max(y, 0), min(x + w, img.width), min(y + h, img.height))
        img = img.crop(box)
        ox, oy = box[0], box[1]
    if zoom != 1:   # whole pixels, as the program draws them
        img = img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)
    if not frames:
        return img
    r = BADGE
    margin = 0 if inside else 2 * r + 12
    if top:
        out = Image.new("RGBA", (img.width, img.height + margin), (0, 0, 0, 0))
        out.paste(img, (0, margin))
    else:
        out = Image.new("RGBA", (img.width + margin, img.height), (0, 0, 0, 0))
        out.paste(img, (margin, 0))
    d = ImageDraw.Draw(out)
    font = ImageFont.truetype(str(ROOT / "DejaVuSansMono.ttf"), 15)
    for fr in frames:
        x, y, w, h = [v * zoom for v in fr["rect"]]
        x -= ox * zoom
        y -= oy * zoom
        if top:
            y += margin
        else:
            x += margin
        # inside the widget by a pixel or two, so neighbours' frames do not overlap
        d.rounded_rectangle([x + 2, y + 2, x + w - 3, y + h - 3], radius=4, outline=ACCENT, width=3)
        if inside:
            # "at": where the number goes from the frame's corner, for a frame too
            # small to hold it
            at = fr.get("at", [r + 4, r + 4])
            dx, dy = {"left": [-14, 12], "right": [w + 14, 12]}.get(at, at) if isinstance(at, str) else at
            cx, cy = x + dx, y + dy
        elif top:
            # "at": over the frame's "left" or "right" end, or that far from its left,
            # when two frames side by side would put their numbers together
            at = fr.get("at", w // 2)
            dx = {"left": r + 6, "right": w - r - 6}.get(at, w // 2) if isinstance(at, str) else at
            cx, cy = x + dx, r + 2
            d.line([cx, cy + r, cx, y + 3], fill=ACCENT, width=3)
        else:
            # level with a short frame's middle, near a tall one's top
            cy = y + h // 2 if h <= 4 * r else y + 2 * r
            cy = min(max(cy, r + 1), out.height - r - 2)
            cx = r + 2
            d.line([cx + r, cy, x + 3, cy], fill=ACCENT, width=3)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=ACCENT, outline=(255, 255, 255), width=2)
        d.text((cx, cy), str(fr["n"]), font=font, fill=(255, 255, 255), anchor="mm",
            stroke_width=1, stroke_fill=(255, 255, 255))
    return out


def fetch(name, src, media):
    import hashlib
    import io
    import zipfile
    url = src["url"]
    cache = ROOT / "build" / "manual-files" / url.rsplit("/", 1)[-1]
    if not cache.exists():
        print(f"  download {url}")
        cache.parent.mkdir(parents=True, exist_ok=True)
        # curl, not urllib: Python's own certificate check fails on some sites a browser opens;
        # and a browser's name, as some close the connection on curl's own
        if subprocess.run(["curl", "-fsSL", "-A", "Mozilla/5.0", "-o", str(cache), url]).returncode:
            cache.unlink(missing_ok=True)
            sys.exit(f"cannot download {url}")
    data = cache.read_bytes()
    if hashlib.sha256(data).hexdigest() != src["sha256"]:
        sys.exit(f"{cache}: not the file the shot was made with (sha256)")
    if cache.suffix.lower() == ".zip":
        data = zipfile.ZipFile(io.BytesIO(data)).read(src.get("member", name))
    (media / name).write_bytes(data)


def shoot(src, name, exe):
    spec = json.loads((src / "shots" / f"{name}.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        cfg = Path(tmp) / "config"
        shutil.copytree(exe.parent / "config", cfg)
        # {media}: the tape and disk of shots/make_media.py, made afresh for each shot.
        # Not in tmp: the Disk Manager shows the path, and a temp path has the user's name
        if os.name == "nt":
            media = Path(os.environ.get("PUBLIC", "C:\\Users\\Public")) / "Documents" / "Xpeccy+ manual"
        else:
            media = Path("/tmp/xpeccy-manual")
        num = write_config(cfg, {sec: {k: str(v).replace("{media}", str(media)) for k, v in keys.items()}
            for sec, keys in spec.get("config", {}).items()})
        # taken already: the program would move to the next one, and the
        # commands would go to whatever holds this one
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", num)) == 0:
                sys.exit(f"port {num} is in use by another program")
        sys.path.insert(0, str(src / "shots"))
        import make_media
        make_media.main(media)
        for fname, fsrc in spec.get("files", {}).items():
            fetch(fname, fsrc, media)
        prep = [a.replace("{media}", str(media)) for a in spec.get("prepare", [])]
        if prep:
            res = subprocess.run([str(c) for c in [exe, "--confdir", cfg, "--no-log"] + prep],
                cwd=exe.parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
            if res.returncode:
                sys.exit(f"prepare: exit code {res.returncode}")
        args = [a.replace("{media}", str(media)) for a in spec.get("args", [])]
        cmd = [exe, "--confdir", cfg, "--no-log"] + args
        if os.name == "nt" and not os.environ.get("XP_VISIBLE"):
            pid, handle = start_hidden(cmd, exe.parent)
        else:
            pid, handle = subprocess.Popen([str(c) for c in cmd], cwd=exe.parent), None
        try:
            port = Port(num)
            for step in spec.get("steps", []):
                if step[0] == "wait":
                    time.sleep(step[1] / 1000)
                elif step[0] == "click-popup":
                    # into the menu that is open, dx dy from its corner; what the item
                    # opens may hold the answer, so the next commands go on a new line
                    ans = port.run(f"winshot {Path(tmp) / 'where.png'}").split()
                    if not ans or ans[0] != "ok":
                        sys.exit(f"winshot: {' '.join(ans)}")
                    port.sock.sendall(f"click {int(ans[1]) + step[1]} {int(ans[2]) + step[2]}\n".encode())
                    port.held.append(port.sock)
                    port.connect()
                    print(f"  click-popup: sent")
                elif step[0] in ("click", "click-nowait"):
                    button = step[4] if len(step) > 4 else ""
                    print(f"  {step[0]} {step[1]}: {click_in(port, *step[1:4], tmp, step[0] == 'click', button)}")
                else:
                    print(f"  {step[1]}: {port.run(step[1])}")
            frames = [dict(fr, rect=fr.get("rect") or union([rect_of(port, w)
                for w in fr.get("widgets", [fr.get("widget")])])) for fr in spec.get("frames", [])]
            crop = spec.get("crop")
            if isinstance(crop, dict) and ("widget" in crop or "widgets" in crop):
                rects = [rect_of(port, w) for w in crop.get("widgets", [crop.get("widget")])]
                p = crop.get("pad", 0)
                x0 = min(r[0] for r in rects) - p
                y0 = min(r[1] for r in rects) - p
                x1 = max(r[0] + r[2] for r in rects) + p
                y1 = max(r[1] + r[3] for r in rects) + p
                crop = [x0, y0, x1 - x0, y1 - y0]
            raw = Path(tmp) / "raw.png"
            menus = spec.get("capture") == "menus"
            ans = port.run(f"{'winshot-menus' if menus else 'winshot'} {raw}")
            if not ans.startswith("ok"):
                sys.exit(f"winshot: {ans}")
            if isinstance(crop, dict) and "menus" in crop:
                # around the open menus; "top" takes it up to the window's top edge,
                # where the menu bar is
                rects = [[int(v) for v in ln.split()[1:5]] for ln in ans.splitlines() if ln.startswith("menu ")]
                if not rects:
                    sys.exit("no menu is open")
                p = crop.get("pad", 16)
                x0 = min(r[0] for r in rects) - p
                y0 = 0 if crop.get("top") else min(r[1] for r in rects) - p
                x1 = max(r[0] + r[2] for r in rects) + p
                y1 = max(r[1] + r[3] for r in rects) + p
                crop = [x0, y0, x1 - x0, y1 - y0]
        finally:
            stop(pid, handle)
            time.sleep(0.5)     # the files are let go of with the process
            shutil.rmtree(media, ignore_errors=True)
        from PIL import Image
        img = annotate(Image.open(raw).convert("RGBA"), frames, crop,
            spec.get("zoom", 1), spec.get("badges") == "top", spec.get("badges") == "inside")
        out = src / "images" / f"{name}.png"
        out.parent.mkdir(exist_ok=True)
        img.save(out, optimize=True)
        print(f"  {out.relative_to(ROOT)} {img.width}x{img.height}")


def main(src, names):
    exe = find_exe()
    print(f"program: {exe}")
    if not names:
        names = sorted(p.stem for p in (src / "shots").glob("*.json"))
    for name in names:
        print(name)
        shoot(src, name, exe)
