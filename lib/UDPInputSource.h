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

#ifndef _LIBFC_UDPINPUTSOURCE_H_
#define _LIBFC_UDPINPUTSOURCE_H_

#include "InputSource.h"

namespace libfc {

class UDPInputSource : public InputSource {
public:
  /** Creates a UDP input source from a file descriptor.
   *
   * @param remote the socket address of the peer from whom we accept messages
   * @param remote_len the length of the socket address, in bytes
   * @param fd the file descriptor belonging to a UDP socket
   */
  UDPInputSource(const struct sockaddr *remote, size_t remote_len, int fd);

  ssize_t read(uint8_t *buf, uint16_t len);
  bool resync();
  size_t get_message_offset() const;
  void advance_message_offset();
  const char *get_name() const;
  bool can_peek() const;

private:
  uint8_t packet_buffer[4096];
  ssize_t packet_lenght = 0;
  ssize_t packet_read = 0;

  struct sockaddr remote;
  size_t remote_len;
  int fd;
};

} // namespace libfc

#endif // _LIBFC_UDPINPUTSOURCE_H_
