1. Describe how your exchange works.

2. Describe your design decisions for the trader and how it's fault-tolerant.

3. Describe your tests and how to run them.

1. race signal when recevie multiple sigusr1 at the same time
2. invalid if buy or sell not in book
3. 

for trader in traders do:
    message market open to trader

for trader in traders do
    Send sigusr1 to trader

signal handler shouldn't be long
