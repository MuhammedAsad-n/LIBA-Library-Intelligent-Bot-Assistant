"""
LIBA Control Station — desktop GUI
A custom industrial-inspired interface in graphite, warm ivory, and signal orange.
"""
import threading, queue, time, tkinter as tk
from tkinter import ttk, messagebox

try:
    import websocket
except ImportError:
    websocket = None

BG = "#11191D"
PANEL = "#1B262B"
PANEL_ALT = "#202D32"
BORDER = "#33434A"
TEXT = "#F2F0E8"
MUTED = "#A5B2B5"
ORANGE = "#F29B38"
ORANGE_DARK = "#C8751E"
TEAL = "#6BC4B8"
RED = "#E56B5D"
GREEN = "#7DC69B"
FONT = "Segoe UI"

class LIBAControlStation:
    def __init__(self, root):
        self.root=root
        root.title("LIBA / Control Station")
        root.geometry("1160x760")
        root.minsize(1000,680)
        root.configure(bg=BG)
        self.ws=None; self.connected=False; self.events=queue.Queue()
        self._style()
        self._build()
        root.after(100,self._poll)
        root.protocol("WM_DELETE_WINDOW",self.close)

    def _style(self):
        s=ttk.Style()
        try: s.theme_use("clam")
        except tk.TclError: pass
        s.configure("TFrame",background=BG)
        s.configure("Panel.TFrame",background=PANEL)
        s.configure("PanelAlt.TFrame",background=PANEL_ALT)
        s.configure("TLabel",background=BG,foreground=TEXT,font=(FONT,10))
        s.configure("Panel.TLabel",background=PANEL,foreground=TEXT,font=(FONT,10))
        s.configure("Muted.TLabel",background=PANEL,foreground=MUTED,font=(FONT,9))
        s.configure("Header.TLabel",background=BG,foreground=TEXT,font=(FONT,20,"bold"))
        s.configure("Sub.TLabel",background=BG,foreground=MUTED,font=(FONT,10))
        s.configure("CardTitle.TLabel",background=PANEL,foreground=MUTED,font=(FONT,9,"bold"))
        s.configure("Accent.TLabel",background=PANEL,foreground=ORANGE,font=(FONT,18,"bold"))
        s.configure("TButton",font=(FONT,10,"bold"),padding=(10,8),background=PANEL_ALT,foreground=TEXT)
        s.map("TButton",background=[("active",BORDER)],foreground=[("active",TEXT)])
        s.configure("Accent.TButton",background=ORANGE,foreground="#172025")
        s.map("Accent.TButton",background=[("active",ORANGE_DARK)])
        s.configure("Danger.TButton",background=RED,foreground="#201716")
        s.map("Danger.TButton",background=[("active","#B84F45")])
        s.configure("TEntry",fieldbackground="#0D1417",foreground=TEXT,insertcolor=TEXT,bordercolor=BORDER)
        s.configure("TCombobox",fieldbackground="#0D1417",foreground=TEXT)
        s.configure("Horizontal.TScale",background=PANEL,troughcolor="#0D1417")
        s.configure("TSeparator",background=BORDER)

    def panel(self,parent,title=None):
        f=ttk.Frame(parent,style="Panel.TFrame",padding=15)
        if title:
            ttk.Label(f,text=title.upper(),style="CardTitle.TLabel").pack(anchor="w",pady=(0,10))
        return f

    def _build(self):
        # Top bar
        header=ttk.Frame(self.root,padding=(24,20,24,14)); header.pack(fill="x")
        titlebox=ttk.Frame(header); titlebox.pack(side="left")
        ttk.Label(titlebox,text="LIBA",font=(FONT,24,"bold"),foreground=ORANGE,background=BG).pack(side="left")
        ttk.Label(titlebox,text="  /  CONTROL STATION",font=(FONT,11,"bold"),foreground=TEXT,background=BG).pack(side="left",pady=(8,0))
        ttk.Label(header,text="LIBRARY INTELLIGENT BOT ASSISTANT",style="Sub.TLabel").pack(side="right",pady=(8,0))
        ttk.Separator(self.root).pack(fill="x",padx=24)

        # Connection strip
        conn=ttk.Frame(self.root,padding=(24,12)); conn.pack(fill="x")
        ttk.Label(conn,text="LINK",font=(FONT,9,"bold"),foreground=MUTED,background=BG).pack(side="left")
        self.ip=tk.StringVar(value="192.168.4.1")
        ttk.Entry(conn,textvariable=self.ip,width=22).pack(side="left",padx=(12,6))
        self.connect_btn=ttk.Button(conn,text="CONNECT",style="Accent.TButton",command=self.connect); self.connect_btn.pack(side="left")
        self.disconnect_btn=ttk.Button(conn,text="DISCONNECT",command=self.disconnect,state="disabled"); self.disconnect_btn.pack(side="left",padx=6)
        self.link=tk.StringVar(value="OFFLINE")
        self.linkdot=tk.Label(conn,text="●",fg=RED,bg=BG,font=(FONT,12)); self.linkdot.pack(side="left",padx=(18,4))
        ttk.Label(conn,textvariable=self.link,foreground=MUTED,background=BG,font=(FONT,9,"bold")).pack(side="left")
        ttk.Label(conn,text="WS  /  PORT 81",foreground=MUTED,background=BG,font=(FONT,9)).pack(side="right")

        main=ttk.Frame(self.root,padding=(24,8,24,20)); main.pack(fill="both",expand=True)
        left=ttk.Frame(main); left.pack(side="left",fill="y",padx=(0,14))
        right=ttk.Frame(main); right.pack(side="left",fill="both",expand=True)

        # Mission card
        m=self.panel(left,"Mission planning"); m.pack(fill="x",pady=(0,12))
        ttk.Label(m,text="DESTINATION",style="Muted.TLabel").pack(anchor="w")
        self.dest=tk.StringVar(value="Shelf 1  /  Stop 2")
        ttk.Combobox(m,textvariable=self.dest,values=("Shelf 1  /  Stop 2","Shelf 2  /  Stop 3"),state="readonly",width=25).pack(fill="x",pady=(5,12))
        ttk.Label(m,text="OPERATION",style="Muted.TLabel").pack(anchor="w")
        ttk.Label(m,text="Retrieve book and return to home station",style="Panel.TLabel",wraplength=240).pack(anchor="w",pady=(4,12))
        ttk.Button(m,text="▶  START RETRIEVAL",style="Accent.TButton",command=self.mission).pack(fill="x")
        ttk.Label(m,text="ROUTE  ·  HOME → SHELF → HOME",style="Muted.TLabel").pack(anchor="w",pady=(10,0))

        # status card
        st=self.panel(left,"Robot state"); st.pack(fill="x",pady=(0,12))
        self.state=tk.StringVar(value="Awaiting connection")
        ttk.Label(st,textvariable=self.state,style="Accent.TLabel",wraplength=230).pack(anchor="w")
        self.state_detail=tk.StringVar(value="Connect to the ESP32 to receive status.")
        ttk.Label(st,textvariable=self.state_detail,style="Muted.TLabel",wraplength=240).pack(anchor="w",pady=(6,0))

        # manual control
        c=self.panel(left,"Arm & motion controls"); c.pack(fill="x",pady=(0,12))
        grid=ttk.Frame(c,style="Panel.TFrame"); grid.pack(fill="x")
        controls=[("ARM / SHELF","1"),("EXTEND  ·  B1","B1"),("RETRACT  ·  R1","R1"),("GRIPPER  ·  X","X"),("HOME  ·  H","H")]
        for i,(label,cmd) in enumerate(controls):
            ttk.Button(grid,text=label,command=lambda x=cmd:self.send(x)).grid(row=i,column=0,sticky="ew",pady=3)
        grid.columnconfigure(0,weight=1)
        speed=self.panel(left,"Drive tuning"); speed.pack(fill="x")
        self.speed=tk.IntVar(value=120)
        ttk.Scale(speed,from_=60,to=255,variable=self.speed,orient="horizontal").pack(fill="x")
        self.speedval=tk.StringVar(value="120")
        ttk.Label(speed,textvariable=self.speedval,style="Accent.TLabel").pack(anchor="e")
        self.speed.trace_add("write",lambda *_:self.speedval.set(str(int(self.speed.get()))))
        ttk.Button(speed,text="APPLY MOTOR SPEED",command=lambda:self.send(f"SPEED:{int(self.speed.get())}")).pack(fill="x",pady=(7,0))

        # Right top metrics
        metrics=ttk.Frame(right); metrics.pack(fill="x",pady=(0,12))
        self.metric_card(metrics,"MISSION","IDLE","mission")
        self.metric_card(metrics,"LOCATION","HOME","location")
        self.metric_card(metrics,"LAST EVENT","—","event")

        # Activity log
        logpanel=self.panel(right,"Mission telemetry / event stream"); logpanel.pack(fill="both",expand=True)
        toolbar=ttk.Frame(logpanel,style="Panel.TFrame"); toolbar.pack(fill="x",pady=(0,8))
        ttk.Label(toolbar,text="LIVE MESSAGE FEED",style="Muted.TLabel").pack(side="left")
        ttk.Button(toolbar,text="CLEAR",command=self.clear).pack(side="right")
        self.log=tk.Text(logpanel,bg="#0D1417",fg="#D7E0DC",insertbackground=TEXT,relief="flat",borderwidth=0,
                         font=("Consolas",10),wrap="word",state="disabled")
        self.log.pack(fill="both",expand=True)
        self.log.tag_configure("time",foreground="#6E858B")
        self.log.tag_configure("send",foreground=ORANGE)
        self.log.tag_configure("receive",foreground=TEAL)
        self.log.tag_configure("error",foreground=RED)
        # Raw command
        raw=ttk.Frame(right,style="Panel.TFrame",padding=10); raw.pack(fill="x",pady=(12,0))
        ttk.Label(raw,text="RAW COMMAND",style="Muted.TLabel").pack(side="left")
        self.raw=tk.StringVar()
        ttk.Entry(raw,textvariable=self.raw).pack(side="left",fill="x",expand=True,padx=8)
        ttk.Button(raw,text="SEND",command=self.rawsend).pack(side="right")
        # Safety footer
        footer=ttk.Frame(right); footer.pack(fill="x",pady=(10,0))
        ttk.Label(footer,text="SAFETY OVERRIDE",foreground=RED,background=BG,font=(FONT,9,"bold")).pack(side="left")
        ttk.Button(footer,text="■  EMERGENCY STOP",style="Danger.TButton",command=self.estop).pack(side="right")
        self.logline("Control station initialized. Firmware connection is not yet active.")

    def metric_card(self,parent,label,value,key):
        f=ttk.Frame(parent,style="Panel.TFrame",padding=12); f.pack(side="left",fill="x",expand=True,padx=(0,8))
        ttk.Label(f,text=label,style="CardTitle.TLabel").pack(anchor="w")
        var=tk.StringVar(value=value); setattr(self,key,var)
        ttk.Label(f,textvariable=var,font=(FONT,15,"bold"),foreground=ORANGE,background=PANEL).pack(anchor="w",pady=(4,0))

    def logline(self,msg,tag=""):
        self.log.configure(state="normal")
        self.log.insert("end",time.strftime("%H:%M:%S")+"  ", "time")
        self.log.insert("end",msg+"\n",tag)
        self.log.see("end"); self.log.configure(state="disabled")

    def connect(self):
        if websocket is None:
            messagebox.showerror("Missing package","Run: python -m pip install -r requirements.txt")
            return
        url="ws://"+self.ip.get().strip()+":81"
        self.connect_btn.configure(state="disabled"); self.link.set("CONNECTING"); self.linkdot.configure(fg=ORANGE)
        self.logline("Opening WebSocket "+url,"send")
        threading.Thread(target=self.worker,args=(url,),daemon=True).start()

    def worker(self,url):
        try:
            sock=websocket.create_connection(url,timeout=5)
            self.ws=sock; self.connected=True; self.events.put(("connected","Connected to ESP32"))
            sock.settimeout(1)
            while self.connected:
                try:
                    msg=sock.recv()
                    if msg: self.events.put(("message",str(msg)))
                except websocket.WebSocketTimeoutException: continue
                except Exception as e:
                    if self.connected:self.events.put(("error",str(e)))
                    break
        except Exception as e:self.events.put(("error",str(e)))
        finally:
            self.connected=False; self.events.put(("disconnected","Connection closed"))

    def _poll(self):
        try:
            while True:
                typ,msg=self.events.get_nowait()
                if typ=="connected":
                    self.link.set("CONNECTED"); self.linkdot.configure(fg=GREEN)
                    self.connect_btn.configure(state="disabled"); self.disconnect_btn.configure(state="normal")
                    self.state.set("Connected"); self.state_detail.set("ESP32 WebSocket link established.")
                    self.logline(msg,"receive")
                elif typ=="message":
                    self.logline("ESP32  ←  "+msg,"receive")
                    self.event.set(msg) if hasattr(self, "event") else None
                    self.parse_status(msg)
                elif typ=="error":
                    self.link.set("ERROR"); self.linkdot.configure(fg=RED); self.logline("ERROR  "+msg,"error")
                elif typ=="disconnected":
                    self.link.set("OFFLINE"); self.linkdot.configure(fg=RED)
                    self.connect_btn.configure(state="normal"); self.disconnect_btn.configure(state="disabled")
                    self.logline(msg)
        except queue.Empty: pass
        self.root.after(100,self._poll)

    def parse_status(self,msg):
        if msg.startswith("GOING"): self.mission.set("OUTBOUND"); self.state.set("En route"); self.state_detail.set("Following track toward target shelf.")
        elif msg.startswith("STOP:"): self.location.set(msg.split(":")[-1]); self.state.set("Stop marker detected")
        elif msg.startswith("AT TARGET"): self.mission.set("PICKUP"); self.state.set("At target shelf"); self.state_detail.set("Arm sequence active.")
        elif msg.startswith("RETURNING"): self.mission.set("RETURN"); self.state.set("Returning home"); self.state_detail.set("Backtracking to home marker.")
        elif msg.startswith("DELIVERED"): self.mission.set("COMPLETE"); self.location.set("HOME"); self.state.set("Delivery complete"); self.state_detail.set("Mission finished successfully.")
        elif msg=="DONE": self.mission.set("IDLE")
        elif msg=="BUSY": self.state.set("Busy")
        elif msg=="READY": self.state.set("Ready")
        elif msg=="ESTOP": self.mission.set("STOPPED"); self.state.set("Emergency stop active"); self.state_detail.set("Motors stopped by safety command.")
        elif msg.startswith("ERR"): self.state.set("Firmware error"); self.state_detail.set(msg)

    def send(self,cmd):
        if not self.connected or not self.ws:
            messagebox.showinfo("Not connected","Connect to the robot first. This interface can be explored offline.")
            return
        try:self.ws.send(cmd); self.logline("PC  →  "+cmd,"send")
        except Exception as e:self.logline("SEND ERROR "+str(e),"error")

    def mission(self):
        d=self.dest.get()
        stop="2" if "Stop 2" in d else "3"
        self.mission.set("QUEUED"); self.event.set("T1:"+stop) if hasattr(self, "event") else None
        self.send("T1:"+stop)

    def rawsend(self):
        cmd=self.raw.get().strip()
        if cmd:self.send(cmd); self.raw.set("")

    def estop(self):
        if self.connected:self.send("S")
        else:messagebox.showwarning("Offline","No ESP32 is connected. Emergency stop cannot be transmitted.")

    def disconnect(self):
        self.connected=False
        try:
            if self.ws:self.ws.close()
        except:pass
        self.ws=None

    def clear(self):
        self.log.configure(state="normal"); self.log.delete("1.0","end"); self.log.configure(state="disabled")

    def close(self):
        self.disconnect(); self.root.destroy()

if __name__=="__main__":
    root=tk.Tk()
    app=LIBAControlStation(root)
    root.mainloop()
