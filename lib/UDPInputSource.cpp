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
#include <cstring>

#include <sys/socket.h>

#include "UDPInputSource.h"
#include <stdio.h>
namespace libfc {

UDPInputSource::UDPInputSource(const struct sockaddr *_remote,
                               size_t _remote_len, int _fd)
    : remote_len(_remote_len), fd(_fd) {
  memcpy(&remote, _remote, _remote_len);
}

ssize_t UDPInputSource::read(uint8_t *buf, uint16_t len) {
  struct sockaddr received_sa;
  socklen_t received_sa_len;

  // Wait for a packet if needed
  if (packet_read == packet_lenght) {
    packet_lenght = recvfrom(fd, packet_buffer, sizeof(packet_buffer), 0,
                             &received_sa, &received_sa_len);
    packet_read = 0;
  }

  if (packet_read + len <= packet_lenght) {
    memcpy(buf, packet_buffer + packet_read, len);
    packet_read += len;
  } else {
    return -1;
  }

  return len;
}

bool UDPInputSource::resync() {
  packet_read = 0;
  packet_lenght = 0;
  return true;
}

size_t UDPInputSource::get_message_offset() const { return 0; }

void UDPInputSource::advance_message_offset() {}

const char *UDPInputSource::get_name() const { return "<UDP socket>"; }

bool UDPInputSource::can_peek() const { return false; }

} // namespace libfc
