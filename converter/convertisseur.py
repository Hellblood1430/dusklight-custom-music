#!/usr/bin/env python3
"""
Convertisseur de musiques pour Dusklight - application portable.

Choisis tes musiques (mp3, wav, ogg, flac...), regle le volume, clique sur
"Convertir" : les fichiers .ast sont crees directement dans le dossier "music"
du mod, avec le meme nom que tes fichiers.

ffmpeg est integre a l'application (via le paquet imageio-ffmpeg) : rien a installer.
"""
import array
import os
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import wave

APP_TITLE = "Convertisseur de musiques pour Dusklight"
RATE = 32000                       # frequence des musiques du jeu
BLOCK_BYTES_PER_CHANNEL = 0x2760   # taille de bloc classique des AST
HEADER_SIZE = 0x40
BLOCK_HEADER_SIZE = 0x20
AUDIO_EXTS = (".wav", ".mp3", ".ogg", ".flac", ".m4a", ".opus", ".aac", ".wma", ".mp4", ".webm")


# ---------------------------------------------------------------------------
# Moteur de conversion (sans interface)
# ---------------------------------------------------------------------------

def find_ffmpeg():
    """Cherche ffmpeg : d'abord celui integre a l'application, sinon celui du systeme."""
    try:
        import imageio_ffmpeg
        path = imageio_ffmpeg.get_ffmpeg_exe()
        if path and os.path.exists(path):
            return path
    except Exception:
        pass
    return shutil.which("ffmpeg")


def decode_to_wav(src, ffmpeg, gain_db):
    """Decode n'importe quel fichier audio en WAV 16 bits stereo 32000 Hz (volume ajuste)."""
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    cmd = [ffmpeg, "-y", "-loglevel", "error", "-i", src, "-vn",
           "-ar", str(RATE), "-ac", "2",
           "-af", "volume=%.1fdB" % gain_db,
           "-c:a", "pcm_s16le", tmp.name]
    kwargs = {}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    try:
        res = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, **kwargs)
    except OSError as e:
        os.remove(tmp.name)
        raise RuntimeError("Impossible de lancer ffmpeg : %s" % e)
    if res.returncode != 0:
        os.remove(tmp.name)
        msg = res.stderr.decode("utf-8", "replace").strip().splitlines()
        raise RuntimeError("ffmpeg n'a pas pu lire ce fichier. " + (msg[-1] if msg else ""))
    return tmp.name


def build_ast(samples, rate, loop):
    """samples : array('h') stereo entrelace (little-endian en memoire). Retourne les octets du .ast."""
    total = len(samples) // 2
    if total == 0:
        raise RuntimeError("Le fichier audio est vide.")
    spb = BLOCK_BYTES_PER_CHANNEL // 2

    left = samples[0::2]
    right = samples[1::2]
    if sys.byteorder == "little":  # l'AST stocke ses echantillons PCM16 en big-endian
        left.byteswap()
        right.byteswap()

    blocks = bytearray()
    pos = 0
    while pos < total:
        n = min(spb, total - pos)
        blocks += b"BLCK" + struct.pack(">I", n * 2) + bytes(BLOCK_HEADER_SIZE - 8)
        blocks += left[pos:pos + n].tobytes()
        blocks += right[pos:pos + n].tobytes()
        pos += n

    header = bytearray(HEADER_SIZE)
    header[0:4] = b"STRM"
    struct.pack_into(">I", header, 0x04, len(blocks))
    struct.pack_into(">H", header, 0x08, 1)                       # 1 = PCM16
    struct.pack_into(">H", header, 0x0A, 16)
    struct.pack_into(">H", header, 0x0C, 2)                       # 2 canaux
    struct.pack_into(">H", header, 0x0E, 0xFFFF if loop else 0)   # boucle
    struct.pack_into(">I", header, 0x10, rate)
    struct.pack_into(">I", header, 0x14, total)
    struct.pack_into(">I", header, 0x18, 0)                       # debut de boucle
    struct.pack_into(">I", header, 0x1C, total)                   # fin de boucle
    struct.pack_into(">I", header, 0x20, BLOCK_BYTES_PER_CHANNEL)
    struct.pack_into(">I", header, 0x24, 0)
    struct.pack_into(">I", header, 0x28, 0x7F000000)              # volume 100 %
    return bytes(header) + bytes(blocks), total


def convert_file(src, out_path, gain_db, loop, ffmpeg):
    """Convertit src en .ast a l'emplacement out_path. Retourne (duree en s, taille en octets)."""
    wav_path = decode_to_wav(src, ffmpeg, gain_db)
    try:
        with wave.open(wav_path, "rb") as w:
            if w.getsampwidth() != 2 or w.getnchannels() != 2 or w.getframerate() != RATE:
                raise RuntimeError("Conversion audio inattendue.")
            raw = w.readframes(w.getnframes())
    finally:
        os.remove(wav_path)
    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder == "big":
        samples.byteswap()
    data, total = build_ast(samples, RATE, loop)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    return total / RATE, len(data)


def default_out_dir():
    """Le dossier 'music' du mod s'il existe, sinon un dossier a cote de l'application."""
    appdata = os.environ.get("APPDATA")
    if appdata:
        base = os.path.join(appdata, "TwilitRealm", "Dusklight", "mod_data", "com.example.my_mod")
        if os.path.isdir(base):
            return os.path.join(base, "music")
    if getattr(sys, "frozen", False):
        here = os.path.dirname(sys.executable)
    else:
        here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "musiques_converties")


def unique_name(stem, used):
    name = stem
    k = 2
    while name.lower() in used:
        name = "%s (%d)" % (stem, k)
        k += 1
    used.add(name.lower())
    return name


# ---------------------------------------------------------------------------
# Interface graphique
# ---------------------------------------------------------------------------

def run_gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    class App:
        def __init__(self, root):
            self.root = root
            self.files = []
            self.queue = queue.Queue()
            self.working = False
            self.ffmpeg = find_ffmpeg()

            root.title(APP_TITLE)
            root.geometry("780x640")
            root.minsize(680, 560)

            pad = {"padx": 12, "pady": 4}

            # 1. fichiers
            ttk.Label(root, text="1. Choisis tes musiques (mp3, wav, ogg, flac...)",
                      font=("Segoe UI", 11, "bold")).pack(anchor="w", **pad)
            bar = ttk.Frame(root)
            bar.pack(fill="x", **pad)
            ttk.Button(bar, text="Ajouter des fichiers...", command=self.add_files).pack(side="left")
            ttk.Button(bar, text="Ajouter un dossier...", command=self.add_folder).pack(side="left", padx=6)
            ttk.Button(bar, text="Retirer la selection", command=self.remove_selected).pack(side="left")
            ttk.Button(bar, text="Tout vider", command=self.clear).pack(side="left", padx=6)

            box = ttk.Frame(root)
            box.pack(fill="both", expand=True, **pad)
            self.listbox = tk.Listbox(box, selectmode="extended", height=9)
            scroll = ttk.Scrollbar(box, orient="vertical", command=self.listbox.yview)
            self.listbox.configure(yscrollcommand=scroll.set)
            self.listbox.pack(side="left", fill="both", expand=True)
            scroll.pack(side="left", fill="y")
            self.count_label = ttk.Label(root, text="0 fichier")
            self.count_label.pack(anchor="w", padx=12)

            # 2. reglages
            ttk.Label(root, text="2. Reglages", font=("Segoe UI", 11, "bold")).pack(anchor="w", **pad)
            row = ttk.Frame(root)
            row.pack(fill="x", **pad)
            ttk.Label(row, text="Volume (dB) :").pack(side="left")
            self.gain = tk.DoubleVar(value=-6.0)
            tk.Scale(row, from_=-20, to=6, resolution=1, orient="horizontal", length=320,
                     variable=self.gain).pack(side="left", padx=8)
            ttk.Label(row, text="(0 = volume d'origine, -6 = moitie moins fort)").pack(side="left")
            self.loop = tk.BooleanVar(value=True)
            ttk.Checkbutton(root, text="Boucler les musiques (recommande)",
                            variable=self.loop).pack(anchor="w", **pad)

            # 3. destination
            ttk.Label(root, text="3. Dossier de destination",
                      font=("Segoe UI", 11, "bold")).pack(anchor="w", **pad)
            row = ttk.Frame(root)
            row.pack(fill="x", **pad)
            self.out_dir = tk.StringVar(value=default_out_dir())
            ttk.Entry(row, textvariable=self.out_dir).pack(side="left", fill="x", expand=True)
            ttk.Button(row, text="Parcourir...", command=self.browse_out).pack(side="left", padx=6)
            ttk.Button(row, text="Ouvrir", command=self.open_out).pack(side="left")

            # 4. lancer
            self.button = ttk.Button(root, text="Convertir", command=self.start)
            self.button.pack(fill="x", padx=12, pady=8, ipady=6)
            self.progress = ttk.Progressbar(root, mode="determinate")
            self.progress.pack(fill="x", padx=12)
            self.log_box = tk.Text(root, height=7, state="disabled", wrap="word")
            self.log_box.pack(fill="both", expand=True, padx=12, pady=8)

            if not self.ffmpeg:
                self.log("ATTENTION : ffmpeg est introuvable, la conversion ne pourra pas fonctionner.")
            self.root.after(100, self.poll)

        # --- liste de fichiers ---
        def refresh(self):
            self.listbox.delete(0, "end")
            for f in self.files:
                self.listbox.insert("end", f)
            n = len(self.files)
            self.count_label.config(text="%d fichier%s" % (n, "s" if n > 1 else ""))

        def add_paths(self, paths):
            for p in paths:
                if p.lower().endswith(AUDIO_EXTS) and p not in self.files:
                    self.files.append(p)
            self.refresh()

        def add_files(self):
            types = [("Fichiers audio", " ".join("*" + e for e in AUDIO_EXTS)), ("Tous les fichiers", "*.*")]
            paths = filedialog.askopenfilenames(title="Choisis tes musiques", filetypes=types)
            self.add_paths(list(paths))

        def add_folder(self):
            folder = filedialog.askdirectory(title="Choisis un dossier de musiques")
            if folder:
                found = []
                for root_dir, _dirs, names in os.walk(folder):
                    for n in sorted(names):
                        found.append(os.path.join(root_dir, n))
                self.add_paths(found)

        def remove_selected(self):
            for i in reversed(self.listbox.curselection()):
                del self.files[i]
            self.refresh()

        def clear(self):
            self.files = []
            self.refresh()

        # --- destination ---
        def browse_out(self):
            folder = filedialog.askdirectory(title="Dossier de destination")
            if folder:
                self.out_dir.set(os.path.normpath(folder))

        def open_out(self):
            folder = self.out_dir.get()
            os.makedirs(folder, exist_ok=True)
            try:
                if os.name == "nt":
                    os.startfile(folder)  # noqa
                else:
                    subprocess.Popen(["xdg-open", folder])
            except Exception as e:
                messagebox.showerror(APP_TITLE, "Impossible d'ouvrir le dossier : %s" % e)

        # --- journal ---
        def log(self, text):
            self.log_box.configure(state="normal")
            self.log_box.insert("end", text + "\n")
            self.log_box.see("end")
            self.log_box.configure(state="disabled")

        # --- conversion ---
        def start(self):
            if self.working:
                return
            if not self.files:
                messagebox.showinfo(APP_TITLE, "Ajoute d'abord des musiques.")
                return
            if not self.ffmpeg:
                messagebox.showerror(APP_TITLE, "ffmpeg est introuvable : conversion impossible.")
                return
            out_dir = self.out_dir.get().strip()
            if not out_dir:
                messagebox.showinfo(APP_TITLE, "Choisis un dossier de destination.")
                return
            try:
                os.makedirs(out_dir, exist_ok=True)
            except OSError as e:
                messagebox.showerror(APP_TITLE, "Dossier de destination impossible : %s" % e)
                return
            self.working = True
            self.button.config(state="disabled", text="Conversion en cours...")
            self.progress.config(maximum=len(self.files), value=0)
            files = list(self.files)
            gain = float(self.gain.get())
            loop = bool(self.loop.get())
            threading.Thread(target=self.worker, args=(files, out_dir, gain, loop), daemon=True).start()

        def worker(self, files, out_dir, gain, loop):
            used = set()
            ok = 0
            for i, src in enumerate(files, 1):
                stem = os.path.splitext(os.path.basename(src))[0]
                name = unique_name(stem, used)
                self.queue.put(("log", "[%d/%d] %s" % (i, len(files), os.path.basename(src))))
                try:
                    secs, size = convert_file(src, os.path.join(out_dir, name + ".ast"), gain, loop, self.ffmpeg)
                    self.queue.put(("log", "     OK : %s.ast (%d min %02d s, %.1f Mo)" %
                                    (name, int(secs // 60), int(secs % 60), size / 1048576)))
                    ok += 1
                except Exception as e:  # noqa
                    self.queue.put(("log", "     ERREUR : %s" % e))
                self.queue.put(("progress", i))
            self.queue.put(("done", (ok, len(files))))

        def poll(self):
            try:
                while True:
                    kind, value = self.queue.get_nowait()
                    if kind == "log":
                        self.log(value)
                    elif kind == "progress":
                        self.progress.config(value=value)
                    elif kind == "done":
                        ok, total = value
                        self.log("Termine : %d/%d musique(s) convertie(s)." % (ok, total))
                        self.log("Relance le jeu pour que le mod les prenne en compte.")
                        self.working = False
                        self.button.config(state="normal", text="Convertir")
            except queue.Empty:
                pass
            self.root.after(100, self.poll)

    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    try:
        run_gui()
    except Exception as exc:  # affichage d'une erreur lisible au lieu d'une fenetre qui disparait
        try:
            import tkinter
            from tkinter import messagebox
            r = tkinter.Tk()
            r.withdraw()
            messagebox.showerror(APP_TITLE, "Erreur inattendue :\n%s" % exc)
        except Exception:
            raise exc
