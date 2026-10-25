# recv_telemetry.py — 핸즈온 2 Step 3: 보드(192.168.77.20:5005)가 보내는 UDP 텔레메트리 수신
#   payload 12 B = struct telemetry_t { uint32 seq; int32 temp_mC; uint32 uptime_ms; } (리틀엔디언)
#   실행: py recv_telemetry.py      (첫 실행 때 Windows 방화벽 '액세스 허용' — 공용 네트워크 체크)
import socket, struct

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 5005))
print("listening UDP 5005 ... (Ctrl+C to stop)")
last = None
while True:
    data, addr = s.recvfrom(64)
    if len(data) != 12:
        print(f"{addr}: unexpected {len(data)} bytes")
        continue
    seq, temp, up = struct.unpack("<IiI", data)
    lost = "" if last is None or seq == last + 1 else f"   (lost {seq - last - 1})"
    last = seq
    print(f"#{seq}  {temp/1000:.2f} C  {up} ms  from {addr[0]}{lost}")
