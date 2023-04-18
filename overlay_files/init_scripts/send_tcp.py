#!/bin/python
import socket
import time

def udp_send():
    for i in range(0, 5):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.168.1.2", 5555))
        s.sendall(str.encode(f"TEST DATA {i}"))
        data = s.recv(1024)
        print(f"Received Data {data.decode()}")
        time.sleep(1)

        

if __name__ == "__main__":
    udp_send()
