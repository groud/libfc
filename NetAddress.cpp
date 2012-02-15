#include <NetAddress.h>
#include <stdexcept>

namespace IPFIX {

int NetAddress::create_socket_sa() {
    /* grab family from sockaddr */
    family_ = reinterpret_cast<struct sockaddr*>(&sa_)->sa_family;
    
    int sock = socket(family_, 
                      socktype(), 
                      proto_);
    if (sock < 0) return sock;
    
    int rv;
    if (passive_) {
        rv = bind(sock, reinterpret_cast<struct sockaddr*>(&sa_), addrlen());
    } else {
        rv = connect(sock, reinterpret_cast<struct sockaddr*>(&sa_), addrlen());
    }
    
    if (rv < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

int NetAddress::create_socket_ai() {
    addrinfo hints, *lai, *ai;
    int ai_error;

    hints.ai_flags = AI_ADDRCONFIG;
    if (passive_) hints.ai_flags |= AI_PASSIVE;
    hints.ai_family = family_;
    hints.ai_socktype = socktype();
    hints.ai_protocol = proto_;

    const char* hostname_cs = 
        (hostname_.length() > 0) ? hostname_.c_str() : NULL;
    const char* servname_cs = 
        (servname_.length() > 0) ? servname_.c_str() : NULL;

    if ((ai_error = getaddrinfo(hostname_cs, servname_cs, &hints, &ai))) {
        // FIXME handle lookup error -- cache this in the object itself?
        return -1;                
    }
    
    int sock;
    for (ai = lai; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, 
                          ai->ai_socktype, 
                          ai->ai_protocol);
        if (sock < 0) continue;

        int rv;
        if (passive_) {
            // FIXME only binds the first bindable
            rv = bind(sock, ai->ai_addr, ai->ai_addrlen);
        } else {
            rv = connect(sock, ai->ai_addr, ai->ai_addrlen);
        }
        if (rv < 0) {
            close(sock);
            sock = -1;
            continue;                    
        }
        
        // store the address
        set_sockaddr(ai->ai_addr, ai->ai_addrlen);
        break;
    }
    
    freeaddrinfo(lai);
    return sock;
}

int NetAddress::socktype() const {
    switch(proto_) {
        case IPPROTO_TCP:
        return SOCK_STREAM;
        case IPPROTO_UDP:
        return SOCK_DGRAM;
        case IPPROTO_SCTP:
        return SOCK_SEQPACKET;
        default:
        return 0;
    }
}

size_t NetAddress::addrlen() const {
    if (!sa_valid_) 
    switch (family_) {
        case PF_INET:
        return sizeof(struct sockaddr_in);
        case PF_INET6:
        return sizeof(struct sockaddr_in6);
        default:
        throw std::invalid_argument("unsupported addrtype");
    }
}

uint16_t NetAddress::port() const {
    if (!sa_valid_) {
        throw std::invalid_argument("can't get a port without a struct sockaddr");
    }
    switch(family_) {
        case PF_INET:
        return ntohs(reinterpret_cast<const struct sockaddr_in*>(&sa_)->sin_port);
        case PF_INET6:
        return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&sa_)->sin6_port);
        default:
        return 0;
    }
}



}