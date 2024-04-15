#pragma once

#include "zedio/socket/net/addr.hpp"
#include "zedio/socket/stream.hpp"

namespace zedio::socket::net {

class TcpStream : public detail::BaseStream<TcpStream, SocketAddr>,
                  public socket::detail::ImplNodelay<TcpStream>,
                  public socket::detail::ImplLinger<TcpStream>,
                  public socket::detail::ImplTTL<TcpStream> {
public:
    explicit TcpStream(const int fd)
        : BaseStream{fd} {
        LOG_DEBUG("Build a TcpStream{{fd: {}}}", fd);
    }

    // TODO: 搞清楚为什么定义了这个析构函数后就编译不通过了?
    // ~TcpStream() {
    //     LOG_DEBUG("Destroy TcpStream");
    // }
};

} // namespace zedio::socket::net
