/* Hi Emacs, please use -*- mode: C++; -*- */
/* Copyright (c) 2011-2014 ETH Zürich. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the names of ETH Zürich nor the names of other contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ETH
 * ZURICH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @author Stephan Neuhaus <neuhaust@tik.ee.ethz.ch>
 */

#ifndef _LIBFC_FILEEXPORTDESTINATION_H_
#define _LIBFC_FILEEXPORTDESTINATION_H_

#if defined(_LIBFC_HAVE_LOG4CPLUS_)
#include <log4cplus/logger.h>
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */

#include "ExportDestination.h"

namespace libfc {

/** IPFIX file outputs. */
class FileExportDestination : public ExportDestination {
public:
  /** Creates a file export destination from an already existing
   * file descriptor.
   *
   * @param fd file descriptor pointing to an open file
   */
  FileExportDestination(int fd);

  ssize_t writev(const std::vector<::iovec> &iovecs);
  int flush();
  bool is_connectionless() const;
  size_t preferred_maximum_message_size() const;

private:
  int fd;
#if defined(_LIBFC_HAVE_LOG4CPLUS_)
  log4cplus::Logger logger;
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
};

} // namespace libfc

#endif // _LIBFC_FILEEXPORTDESTINATION_H_
