"""D.A.S.O Debloat GUI

This is a concise, single-file implementation.
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import threading
import subprocess
import os
import sys
import time

APP_TITLE = "D.A.S.O — Debloater"
PACKAGES_FILE = "packages.txt"


def read_packages(path):
    pkgs = []
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            for ln in f:
                p = ln.strip()
                if p and not p.startswith('#'):
                    pkgs.append(p)
    except Exception:
        pass
    return pkgs


def adb_available():
    try:
        subprocess.run(['adb', 'version'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return True
    except Exception:
        return False


def device_connected():
    try:
        out = subprocess.check_output(['adb', 'devices'], text=True)
        for line in out.splitlines()[1:]:
            if line.strip().endswith('device'):
                return True
        return False
    except Exception:
        return False


class DebloatApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry('960x640')
        self.minsize(820, 480)

        # clean exit
        self.protocol('WM_DELETE_WINDOW', self._on_close)

        self.canvas = tk.Canvas(self, highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)
        self.canvas.bind('<Configure>', self._on_resize)

        self.main = ttk.Frame(self.canvas)
        self._win = self.canvas.create_window(0, 0, anchor='nw', window=self.main)

        style = ttk.Style(self)
        try:
            style.theme_use('clam')
        except Exception:
            pass

        style.configure('Main.TFrame', background='#0b0b0b')
        style.configure('TLabel', background='#0b0b0b', foreground='white')

        self.packages_path = os.path.join(os.path.dirname(__file__), PACKAGES_FILE)
        self.packages = []

        # try to set window icon (png for tk, ico for Windows exe and taskbar)
        base_dir = getattr(sys, '_MEIPASS', os.path.dirname(__file__))
        assets_dir = os.path.join(base_dir, 'assets')
        ico_path = os.path.join(assets_dir, 'app_icon.ico')
        png_path = os.path.join(assets_dir, 'app_icon.png')
        try:
            if os.path.exists(png_path):
                try:
                    img = tk.PhotoImage(file=png_path)
                    self.iconphoto(False, img)
                except Exception:
                    pass
            if os.path.exists(ico_path):
                try:
                    # Windows: set small symbolic icon
                    self.iconbitmap(ico_path)
                except Exception:
                    pass
        except Exception:
            pass

        self._build_ui()
        self.load_packages(self.packages_path)

        self.after(200, lambda: messagebox.showinfo(APP_TITLE, 'Aplicación desarrollada por Liebe Black'))

    def _on_close(self):
        try:
            self.quit()
        finally:
            self.destroy()

    def _on_resize(self, ev):
        w, h = ev.width, ev.height
        self.canvas.delete('bg')
        self._draw_gradient(self.canvas, w, h, '#0b3d91', '#000000')
        self.canvas.coords(self._win, 0, 0)

    def _draw_gradient(self, canvas, w, h, c1, c2):
        def hex_to_rgb(h):
            h = h.lstrip('#')
            return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)

        r1, g1, b1 = hex_to_rgb(c1)
        r2, g2, b2 = hex_to_rgb(c2)
        steps = max(h, 2)
        for i in range(steps):
            r = int(r1 + (r2 - r1) * i / steps)
            g = int(g1 + (g2 - g1) * i / steps)
            b = int(b1 + (b2 - b1) * i / steps)
            canvas.create_line(0, i, w, i, fill=f'#%02x%02x%02x' % (r, g, b), tag='bg')

    def _build_ui(self):
        f = self.main
        f.config(style='Main.TFrame')
        f.pack(fill='both', expand=True, padx=12, pady=12)

        left = ttk.Frame(f)
        left.pack(side='left', fill='both', expand=True, padx=(0, 8))

        top = ttk.Frame(left)
        top.pack(fill='x')
        ttk.Button(top, text='Cargar paquetes...', command=self.choose_file).pack(side='left')
        ttk.Button(top, text='Seleccionar todo', command=self.select_all).pack(side='left', padx=6)
        ttk.Button(top, text='Limpiar selección', command=self.deselect_all).pack(side='left')

        filter_row = ttk.Frame(left)
        filter_row.pack(fill='x', pady=(8, 4))
        ttk.Label(filter_row, text='Filtrar:').pack(side='left')
        self.var_filter = tk.StringVar()
        ent = tk.Entry(filter_row, textvariable=self.var_filter, bg='#1a1a1a', fg='white', insertbackground='white')
        ent.pack(side='left', fill='x', expand=True, padx=(6, 0))
        ent.bind('<KeyRelease>', lambda e: self.apply_filter())

        list_frame = ttk.Frame(left)
        list_frame.pack(fill='both', expand=True)
        self.listbox = tk.Listbox(list_frame, selectmode='extended', bg='#121212', fg='white', selectbackground='#2a5dca')
        self.listbox.pack(side='left', fill='both', expand=True)
        sb = ttk.Scrollbar(list_frame, orient='vertical', command=self.listbox.yview)
        sb.pack(side='right', fill='y')
        self.listbox.config(yscrollcommand=sb.set)

        right = ttk.Frame(f, width=360)
        right.pack(side='right', fill='y')

        opts = ttk.Frame(right)
        opts.pack(fill='x')
        ttk.Button(opts, text='Verificar ADB', command=self.action_check_adb).pack(fill='x', pady=8)
        self.btn_disable = ttk.Button(opts, text='Deshabilitar (pm disable-user)', command=lambda: self.start_operation('disable'))
        self.btn_disable.pack(fill='x', pady=4)
        self.btn_uninstall = ttk.Button(opts, text='Desinstalar (pm uninstall)', command=lambda: self.start_operation('uninstall'))
        self.btn_uninstall.pack(fill='x')
        ttk.Button(opts, text='Guardar log', command=self.save_log).pack(fill='x', pady=(8, 0))

        ttk.Label(right, text='Progreso:').pack(anchor='w', pady=(12, 0))
        self.progress = ttk.Progressbar(right, orient='horizontal', mode='determinate')
        self.progress.pack(fill='x', pady=6)

        self.status = tk.StringVar(value='Listo')
        ttk.Label(right, textvariable=self.status, relief='sunken').pack(fill='x', pady=(6, 10))

        ttk.Label(right, text='Log:').pack(anchor='w')
        self.log = scrolledtext.ScrolledText(right, height=18, bg='#0b0b0b', fg='white', insertbackground='white')
        self.log.pack(fill='both', expand=True)

    def _append_log(self, text):
        def _do():
            ts = time.strftime('%Y-%m-%d %H:%M:%S')
            self.log.insert('end', f'[{ts}] {text}\n')
            self.log.see('end')
        self.after(0, _do)

    def _set_status(self, text):
        self.after(0, lambda: self.status.set(text))

    def _set_progress(self, val, maximum=None):
        def _do():
            if maximum is not None:
                self.progress['maximum'] = maximum
            self.progress['value'] = val
        self.after(0, _do)

    def choose_file(self):
        p = filedialog.askopenfilename(initialdir=os.path.dirname(self.packages_path), filetypes=[('Text files', '*.txt'), ('All files', '*')])
        if p:
            self.packages_path = p
            self.load_packages(p)

    def load_packages(self, path):
        self.listbox.delete(0, 'end')
        self.packages = read_packages(path)
        for p in self.packages:
            self.listbox.insert('end', p)
        self._append_log(f'Cargados {len(self.packages)} paquetes desde {os.path.basename(path)}')

    def apply_filter(self):
        t = (self.var_filter.get() or '').lower().strip()
        self.listbox.delete(0, 'end')
        if not t:
            for p in self.packages:
                self.listbox.insert('end', p)
            return
        for p in self.packages:
            if t in p.lower():
                self.listbox.insert('end', p)

    def select_all(self):
        self.listbox.select_set(0, 'end')

    def deselect_all(self):
        self.listbox.select_clear(0, 'end')

    def action_check_adb(self):
        if not adb_available():
            messagebox.showerror(APP_TITLE, 'adb no está disponible en PATH')
            self._append_log('adb no disponible')
            return
        if not device_connected():
            messagebox.showwarning(APP_TITLE, 'No hay dispositivos conectados (adb devices)')
            self._append_log('No hay dispositivos conectados')
            return
        messagebox.showinfo(APP_TITLE, 'ADB disponible y dispositivo conectado')
        self._append_log('adb OK y dispositivo conectado')
        self._set_status('ADB OK — dispositivo conectado')

    def start_operation(self, mode):
        sel = [self.listbox.get(i) for i in self.listbox.curselection()]
        if not sel:
            if messagebox.askyesno(APP_TITLE, 'No hay selección. ¿Operar sobre TODOS los paquetes cargados?'):
                sel = list(self.packages)
            else:
                return

        if not adb_available():
            messagebox.showerror(APP_TITLE, 'adb no encontrado en PATH')
            return

        if not device_connected():
            if not messagebox.askyesno(APP_TITLE, 'No hay dispositivo conectado. Continuar de todas formas?'):
                return

        if not messagebox.askyesno(APP_TITLE, f'Confirmar {mode} de {len(sel)} paquetes?'):
            return

        self.btn_disable.state(['disabled'])
        self.btn_uninstall.state(['disabled'])
        self._set_status(f'Ejecutando {mode} ({len(sel)})...')

        thr = threading.Thread(target=self._run_packages, args=(sel, mode), daemon=True)
        thr.start()

    def _run_packages(self, packages, mode):
        total = len(packages)
        success = 0
        failed = 0
        self._set_progress(0, maximum=total)

        for idx, pkg in enumerate(packages, start=1):
            if mode == 'disable':
                cmd = ['adb', 'shell', 'pm', 'disable-user', '--user', '0', pkg]
            else:
                cmd = ['adb', 'shell', 'pm', 'uninstall', '--user', '0', pkg]

            try:
                p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
                out = (p.stdout or '').strip()
                err = (p.stderr or '').strip()
                if p.returncode == 0:
                    success += 1
                    self._append_log(f'{pkg} OK: {out or "(sin salida)"}')
                else:
                    failed += 1
                    self._append_log(f'{pkg} ERROR: {out} {err}')
            except Exception as e:
                failed += 1
                self._append_log(f'{pkg} EXCEPCION: {e}')

            self._set_progress(idx)
            self._set_status(f'{idx}/{total} — {success} OK, {failed} fallos')
            time.sleep(0.05)

        self._append_log('Operación finalizada')
        self._set_status(f'Finalizado — {success} OK, {failed} fallos')
        try:
            self.btn_disable.state(['!disabled'])
            self.btn_uninstall.state(['!disabled'])
        except Exception:
            pass

    def save_log(self):
        p = filedialog.asksaveasfilename(defaultextension='.log', filetypes=[('Log files', '*.log'), ('All files', '*')])
        if p:
            try:
                with open(p, 'w', encoding='utf-8') as f:
                    f.write(self.log.get('1.0', 'end'))
                self._append_log(f'Log guardado en {p}')
            except Exception as e:
                messagebox.showerror(APP_TITLE, f'Error guardando log: {e}')


if __name__ == '__main__':
    app = DebloatApp()
    app.mainloop()
