import socket

host = "127.0.0.1"
port = 1080
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((host, port))
    print("connected")
    sock.send(b"\x05\x01\x00")
    print("sent handshake")
    handshake_response = sock.recv(2)
    if handshake_response == b'\x05\x00':
        print("handshake success!")
    else:
        print("handshake failed!")
        exit(-1)

    # 0x05: 版本
    # 0x01: 命令类型 connect
    # 0x00: 保留字段
    # 0x03: 地址类型 域名
    # 0x09: 目标地址长度
    # baidu.com
    # \x00P: 目标端口 80
    sock.send(b"\x05\x01\x00\x03\x09baidu.com\x00P")
    print(sock.recv(1024))
    sock.send(b"GET / HTTP/1.1\r\nHost: baidu.com\r\n\r\n")
    print("----------------")
    print(sock.recv(1024))

except OSError as e:
    print(e)
finally:
    sock.close()
