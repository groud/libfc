
//
// Created by rlapeyra on 16/08/17.
//
#include <cstring>

#include <sys/socket.h>
#include "Constants.h"
#include "errno.h"
#include <stdlib.h>
#include "stdio.h"
#include <sys/uio.h>
#include "UDPExportDestination.h"
#if defined(_LIBFC_HAVE_LOG4CPLUS_)
#  include <log4cplus/loggingmacros.h>
#else
#  define LOG4CPLUS_TRACE(logger, expr)
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */

namespace libfc {

    UDPExportDestination::UDPExportDestination( const struct  sockaddr * _sa, size_t _sa_len, int _fd)
            : sa_len(_sa_len), fd(_fd)
#if defined(_LIBFC_HAVE_LOG4CPLUS_)
    , logger(log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("FileExportDestination")))
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
    {
        memcpy(&sa, _sa, sa_len);
        msg.msg_name = &sa;
        msg.msg_namelen = sizeof(sa);
        msg.msg_control=nullptr;
        msg.msg_controllen=0;
    }

    ssize_t UDPExportDestination::writev(const std::vector< ::iovec>& iovecs) {
        LOG4CPLUS_TRACE(logger, "ENTER UDPExportDestination::writev");
        LOG4CPLUS_TRACE(logger, "writing " << iovecs.size() << " iovecs");
#if defined(_LIBFC_HAVE_LOG4CPLUS_)
        const ::iovec* vecs = iovecs.data();
    size_t total = 0;
    for (unsigned int i = 0; i < iovecs.size(); i++) {
      LOG4CPLUS_TRACE(logger, "  iovec[" << i << "]"
                      << "@" << vecs[i].iov_base
                      << "[" << vecs[i].iov_len << "]");
      total += vecs[i].iov_len;
    }
    LOG4CPLUS_TRACE(logger, "total=" << total);
#endif /*  defined(_LIBFC_HAVE_LOG4CPLUS_) */
        // create message header

        msg.msg_iov = (iovec *) iovecs.data();
        msg.msg_iovlen = static_cast<int>(iovecs.size());

        sendmsg(fd, &mg, 0);
        return NULL;
        //return ::writev(fd, iovecs.data(), static_cast<int>(iovecs.size()));
    }

    int UDPExportDestination::flush() {
        return 0;
    }
    bool UDPExportDestination::is_connectionless() const {
        return true;
    }

    size_t UDPExportDestination::preferred_maximum_message_size() const {
        return kMaxMessageLen;
    }

}