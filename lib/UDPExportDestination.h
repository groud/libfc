//
// Created by rlapeyra on 16/08/17.
//

#ifndef EXTRACTFEATURES_NOSLIDING_WINDOW_OSNT_UDPOUTPUTSOURCE_H
#define EXTRACTFEATURES_NOSLIDING_WINDOW_OSNT_UDPOUTPUTSOURCE_H

#endif //EXTRACTFEATURES_NOSLIDING_WINDOW_OSNT_UDPOUTPUTSOURCE_H

#  include "OutputSource.h"
#include "ExportDestination.h"

namespace libfc {


    class UDPExportDestination : public ExportDestination {
    public:
        /** Creates a file export destination from an already existing
         * file descriptor.
         *
         * @param fd file descriptor pointing to an open file
         */
        UDPExportDestination(const struct  sockaddr * sa, size_t sa_len, int _fd);

        ssize_t writev(const std::vector<::iovec> &iovecs);

        int flush();

        bool is_connectionless() const;

        size_t preferred_maximum_message_size() const;

    private:
        struct sockaddr sa;
    struct msghdr msg;

    size_t sa_len;
        int fd;
#  if defined(_LIBFC_HAVE_LOG4CPLUS_)
        log4cplus::Logger logger;
#  endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
    };
}
