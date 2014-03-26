#!/usr/bin/env python3

import argparse
import socket

MSG_START = "$".encode(encoding = 'ascii')
MSG_END = "#".encode(encoding = 'ascii')

def gdb_unpack(msg):
    if not msg[0] == MSG_START[0]:
        raise Exception("Unexpected message start")
    if not msg[-3] == MSG_END[0]:
        raise Exception("Unexpected message end")
        
    body = msg[1:-3]
    checksum = sum(body) & 0xff
    
    if checksum != int(msg[-2:], 16):
        raise Exception("Wrong checksum")
        
    return body
    
def gdb_pack(msg):
    if isinstance(msg, str):
        msg = msg.encode(encoding = "ascii")
    MSG_START = "$".encode(encoding = 'ascii')
    MSG_END = "#".encode(encoding = 'ascii')
    checksum = sum(msg) & 0xff
    
    checksum_str = ("%02X" % checksum).encode(encoding = 'ascii')
    
    return MSG_START + msg + MSG_END + checksum_str
    

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", type = int, dest = "port", help = "UDP server port")
    
    args = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.port))
    while True:
        try:
            (msg, address) = sock.recvfrom(4096)
            if msg[0] == "+".encode(encoding = "ascii")[0]:
                continue
            print("DEBUG: %s" % str(msg))
            decoded = gdb_unpack(msg).decode(encoding = 'ascii')
            sock.sendto("+".encode(encoding = "ascii"), address)
            
            print("-> %s" % decoded)
            
            if decoded.startswith("g"):
                reply = ("00000000" * 11) + "EFBEADDE" + ("00000000" * 3) + "00100000" + ("00000000")
            elif decoded.startswith("G"):
                reply = "OK"
            elif decoded.startswith("m"):
                len = int(decoded.split(",")[1])
                reply = "00" * len
            elif decoded.startswith("M"):
                reply = "OK"
            elif decoded.startswith("?"):
                reply = "S05"
            elif decoded.startswith("c"):
                reply = "S05"
            elif decoded.startswith("p"):
                reply = "00000000"
            elif decoded.startswith("P"):
                reply = "OK"
            else:
                reply = ""
            
            print("<- %s" % reply)
            gdb_reply = gdb_pack(reply)
            sock.sendto(gdb_reply, address)
        except Exception as ex:
            print("Exception: %s" % str(ex))

if __name__ == "__main__":
    main()