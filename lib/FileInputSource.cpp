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
#include <sstream>

#include <unistd.h>

#include "FileInputSource.h"

namespace libfc {

FileInputSource::FileInputSource(int fd, std::string file_name)
    : fd(fd), message_offset(0), current_offset(0), file_name(file_name),
      name(0) {}

FileInputSource::~FileInputSource() {
  (void)close(fd); // FIXME: Error handling?
}

ssize_t FileInputSource::read(uint8_t *buf, uint16_t len) {
  ssize_t ret = ::read(fd, buf, len);
  if (ret > 0)
    current_offset += ret;
  return ret;
}

bool FileInputSource::resync() {
  // TODO
  return true;
}

size_t FileInputSource::get_message_offset() const { return message_offset; }

void FileInputSource::advance_message_offset() {
  message_offset += current_offset;
  current_offset = 0;
}

const char *FileInputSource::get_name() const {
  if (name == 0) {
    std::ostringstream sstr;

    sstr << "File(name=\"" << file_name << "\")";
    std::string s = sstr.str();

    name = new char[s.length() + 1];
    std::strcpy(const_cast<char *>(name), s.c_str());
  }

  return name;
}

bool FileInputSource::can_peek() const { return false; }

} // namespace libfc
