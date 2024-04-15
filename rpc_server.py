import socket

# 创建一个socket对象
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# 绑定到特定的地址和端口
server_address = ('localhost', 9000)
server_socket.bind(server_address)

# 监听传入的连接
server_socket.listen(5)

while True:
    # 等待连接
    print('Waiting for a connection...')
    connection, client_address = server_socket.accept()

    try:
        print(f'Connection from {client_address} has been established!')

        # 接收数据
        while True:
            data = connection.recv(1024)
            if not data:
                break
            print(f'Received data: `{data.decode()}`')
            # connection.send(b"\x00\x00\x00\x0chello world!")
            connection.send(b"\x00\x00\x00\x0bzhangsan 189")

    finally:
        # 关闭连接
        connection.close()
