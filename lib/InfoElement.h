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
 * @author Brian Trammell <trammell@tik.ee.ethz.ch>
 *
 * @section DESCRIPTION
 *
 * This file defines InfoElement, which represents an IPFIX Information Element.
 */

#ifndef _LIBFC_INFOELEMENT_H_ // idem
#define _LIBFC_INFOELEMENT_H_ // hack

#include <memory>
#include <stdint.h>
#include <string>

#include "IEType.h"
#include "InfoModel.h"

namespace libfc {

/** An IPFIX Information Element.
 *
 * InfoElements have a name, number, size, and type. To support
 * reduced-length encoding, an InfoElement may keep a reference to
 * different-sized versions of itself.  Canonical InfoElements are
 * stored within an InfoModel, and only canonical InfoElements should
 * be used in IETemplates; use the lookupIE() method on InfoModel to
 * get one for an example (constructed or parsed) InfoElement.
 *
 * This class does not contain any mutator methods; instances of this
 * class are therefore immutable.
 */
class InfoElement {

public:
  /** Forbids creation of empty IEs. */
  InfoElement() = delete;

  /** Creates a new InfoElement by copying an existing one.
   *
   * @param rhs the IE to copy
   */
  InfoElement(const InfoElement &rhs);

  /** Creates a new InfoElement given values for its fields.
   *
   * @param name the IE name
   * @param pen the private enterprise number (0 for IANA IEs)
   * @param number the IE number (with the enterprise bit set to 0)
   * @param ietype pointer to the IEType representing the IE's type; get from
   *               InfoModel::lookupIEType() or one of the IEType statics.
   * @param len the length of the Information Element
   */
  InfoElement(const std::string &name, uint32_t pen, uint16_t number,
              const IEType *ietype, uint16_t len);

  /** Creates a new InfoElement by copying an existing one and
   * changing its size.
   *
   * @param rhs the IE to copy
   * @param nlen the new length
   */
  InfoElement(const InfoElement &rhs, uint16_t nlen);

  /** Gets the IE's name
   *
   * @return the IE's name
   */
  const std::string &name() const;

  /** Gets the IE's private enterprise number.
   *
   * @return the IE's PEN. Returns 0 if the IE is in the IANA registry.
   */
  uint32_t pen() const;

  /** Gets the IE's number.
   *
   * @return the IE's number. The enterprise bit is cleared, even if the IE
   *         is enterprise-specific.
   */
  uint16_t number() const;

  /** Gets a pointer to the IE's type
   *
   * @return a pointer to the IE's type
   */
  const IEType *ietype() const;

  /** Gets the IE's length.
   *
   * @return the IE's length.
   */
  uint16_t len() const;

  /** Gets an IE derived from or identical to this IE for a given
   * length.
   *
   * @return a pointer to the IE's type
   */
  const InfoElement *forLen(uint16_t len) const;

  /** Determines whether two IE's match each other for purposes of
   * template compatibility, based on number and PEN only.
   *
   * @param rhs the InfoElement to compare to
   * @return true if the number and PEN match
   */
  bool matches(const InfoElement &rhs) const;

  /** Gets a complete IESpec for this InfoElement.
   *
   * @return an iespec
   */
  std::string toIESpec() const;

  /** Helper class for hashmapping IE by pointer based on number and PEN.
   */
  class ptrIdHash {
  public:
    std::size_t operator()(const InfoElement *ie) const {
      return ie->pen() | ie->number();
    }
  };

  /** Helper class for comparing IE by pointer based on number and PEN.
   */
  class ptrIdEqual {
  public:
    bool operator()(const InfoElement *lhs, const InfoElement *rhs) const {
      return lhs->matches(*rhs);
    }
  };

private:
  std::string name_;
  uint32_t pen_;
  uint16_t number_;
  const IEType *ietype_;
  uint16_t len_;
  mutable std::map<uint16_t, std::shared_ptr<const InfoElement>> rle_;
  mutable std::string spec;
};
}

#endif // idem hack
