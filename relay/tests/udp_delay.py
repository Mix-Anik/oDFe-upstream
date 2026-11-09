#!/usr/bin/env python3
"""UDP delay proxy for testing the relay on a slow link.

    udp_delay.py <listen port> <target port> [--delay MS] [--jitter MS] [--loss PERCENT] [--seconds N]

Listens on 127.0.0.1:<listen port>; whatever the first sender (the relay) sends goes to 127.0.0.1:<target port> (the remote server)
and the answers go back, each way delayed by DELAY msec plus up to JITTER msec at random (packets are not reordered more than the
jitter makes them) and dropped with the chance LOSS percent. The round trip time is twice the delay.
"""

import argparse
import heapq
import random
import select
import socket
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("listen", type=int)
    ap.add_argument("target", type=int)
    ap.add_argument("--delay", type=float, default=0.0, help="one way delay, msec")
    ap.add_argument("--jitter", type=float, default=0.0, help="extra random delay, msec")
    ap.add_argument("--loss", type=float, default=0.0, help="percent of packets dropped")
    ap.add_argument("--seconds", type=float, default=60.0, help="how long to run")
    args = ap.parse_args()

    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("127.0.0.1", args.listen))
    back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    back.bind(("127.0.0.1", 0))
    target = ("127.0.0.1", args.target)
    client = None

    queue = []      # (due time, serial, socket, destination, data)
    serial = 0
    end = time.time() + args.seconds

    def push(sock, dest, data):
        nonlocal serial
        if args.loss and random.random() * 100.0 < args.loss:
            return
        due = time.time() + (args.delay + random.random() * args.jitter) / 1000.0
        serial += 1
        heapq.heappush(queue, (due, serial, sock, dest, data))

    while time.time() < end:
        wait = 0.001
        if queue:
            wait = max(0.0, min(wait, queue[0][0] - time.time()))
        ready, _, _ = select.select([front, back], [], [], wait)
        for sock in ready:
            try:
                data, addr = sock.recvfrom(65536)
            except OSError:
                continue
            if sock is front:
                client = addr
                push(back, target, data)
            elif client is not None:
                push(front, client, data)
        now = time.time()
        while queue and queue[0][0] <= now:
            _, _, sock, dest, data = heapq.heappop(queue)
            try:
                sock.sendto(data, dest)
            except OSError:
                pass


if __name__ == "__main__":
    main()
