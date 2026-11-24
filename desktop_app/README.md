# LIBA Control Station

Custom desktop interface with a graphite, warm-ivory, and signal-orange visual system. This is a reconstructed Python controller using the ESP32 firmware's WebSocket protocol.

## Run
```powershell
python -m pip install -r requirements.txt
python liba_desktop.py
```
Connect using the ESP32's IP address and WebSocket port 81.

Offline: the interface can be opened and explored, but commands require a robot connection. A local simulator is planned as a separate feature.
