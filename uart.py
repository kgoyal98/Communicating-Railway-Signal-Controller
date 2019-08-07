import serial
import threading

def f1():
	while True:
		a = u1.read()
		u2.write(a)
		print(a)

def f2():
	while True:
		a = u2.read()
		u1.write(a)
		print(a)

u1 = serial.Serial('/dev/ttyXRUSB0', 115200)
u2 = serial.Serial('/dev/ttyXRUSB1', 115200)

t1 = threading.Thread(target=f1)
t2 = threading.Thread(target=f2)
t1.start()
t2.start()
t1.join()
t2.join()
