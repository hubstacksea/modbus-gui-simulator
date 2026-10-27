#!/usr/bin/env python3
"""Creates two linked pseudo terminals (a null-modem cable) for RTU testing.

Prints the two device names and relays bytes until killed.  Equivalent to
`socat -d -d pty,raw,echo=0 pty,raw,echo=0`.
"""
import os
import pty
import select
import sys
import tty

m1, s1 = pty.openpty()
m2, s2 = pty.openpty()
for fd in (s1, s2):
    tty.setraw(fd)
print(os.ttyname(s1), os.ttyname(s2), flush=True)
sys.stdout.close()
while True:
    r, _, _ = select.select([m1, m2], [], [])
    for fd in r:
        try:
            data = os.read(fd, 4096)
        except OSError:
            continue
        os.write(m2 if fd == m1 else m1, data)
