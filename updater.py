import can
from optparse import OptionParser
from time import sleep


def waitForChar(bus, c):
    recv_char = 0
    while recv_char not in c:
       message = bus.recv(0)
       if message and message.arbitration_id == 0x7de:
          recv_char = message.data[0]
          print (recv_char)
    return recv_char


def calcStmCrc(data, idx, len):
    cnt = 0
    crc = 0xffffffff

    while cnt < len:
        word = data[idx] | (data[idx+1] << 8) | (data[idx+2] << 16) | (data[idx+3] << 24)
        cnt = cnt + 4
        idx = idx + 4

        crc = crc ^ word

        for i in range(0, 32):
            if crc & 0x80000000:
                # Polynomial used in STM32
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xffffffff
            else:
                crc = (crc << 1) & 0xffffffff
    return crc


PAGE_SIZE_BYTES = 1024


parser = OptionParser()
parser.add_option("-f", "--file", dest="filename",
                  help="update file")
parser.add_option("-d", "--device", dest="device",
                  help="serial interface")

(options, args) = parser.parse_args()

if not options.filename:   # if filename is not given
    parser.error('Filename not given')
    exit()
if not options.device:   # if device is not given
    parser.error('Device not given')
    exit()

bus=can.interface.Bus(bustype='socketcan', channel=options.device, bitrate=500000)

updateFile = open(options.filename, "rb")
data = bytearray(updateFile.read())
updateFile.close()

numBytes = len(data)
numPages = (numBytes + PAGE_SIZE_BYTES - 1) // PAGE_SIZE_BYTES

while (len(data) % PAGE_SIZE_BYTES) > 0:
    data.append(0)

print("File length is %d bytes/%d pages" % (numBytes, numPages))
print("Resetting device...")

#ser.write(b'reset\r')
version = waitForChar(bus, b'2')

print("Version 2 bootloader, sending magic")
msg = can.Message(arbitration_id=0x7DD, data=[0xAA])
bus.send(msg)
waitForChar(bus, b'S')

print("Sending number of pages...")

msg = can.Message(arbitration_id=0x7DD, data=[numPages])
bus.send(msg)

waitForChar(bus, b'P')

done = False
page = 0
idx = 0

while not done:
    crc = calcStmCrc(data, idx, PAGE_SIZE_BYTES)
    c = 0

    while c != b'P' and not done:
      print("Sending page %d..." % (page), end=' ')

      msg = can.Message(arbitration_id=0x7DD, data=data[idx:idx+8])
      bus.send(msg)
      idx = idx + 8

      c = waitForChar(bus, b'CDEPT')

      if b'C' == c:
         ser.write([crc & 0xFF])
         ser.write([(crc >> 8) & 0xFF])
         ser.write([(crc >> 16) & 0xFF])
         ser.write([(crc >> 24) & 0xFF])
         c = ser.read()
         page = page + 1
         idx = page * PAGE_SIZE_BYTES
      if b'D' == c:
         print("CRC correct!")
         print("Update done!")
         done = True
      elif b'E' == c:
         print("CRC error!")
         waitForChar(ser, b'T')
      elif b'T' == c:
         print("Sync Error!")

