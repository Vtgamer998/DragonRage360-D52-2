#!/usr/bin/env python3
"""Diagnostic echo only; not a matchmaking/game server. Run on PC or Termux."""
import argparse,socket,struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',type=int,default=7370)
a=p.parse_args()
if not 1<=a.port<=65535: p.error('port must be 1..65535')
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
 s.bind(('0.0.0.0',a.port));print(f'UDP echo on port {a.port}; Ctrl+C to stop',flush=True)
 try:
  while True:
   data,addr=s.recvfrom(2048)
   if len(data)==12 and struct.unpack_from('<II',data)==(0x4e335442,5):
    s.sendto(data,addr);print('Echo',addr,flush=True)
 except KeyboardInterrupt: pass
