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

#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <sstream>

#include <time.h>

#ifdef _LIBFC_HAVE_LOG4CPLUS_
#  include <log4cplus/loggingmacros.h>
#else
#  define LOG4CPLUS_TRACE(logger, expr)
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */

#include "decode_util.h"
#include "ipfix_endian.h"

#include "BasicOctetArray.h"
#include "IPFIXContentHandler.h"
#include "PlacementCollector.h"

#include "exceptions/FormatError.h"

/** Decode plans describe how a data record is to be decoded.
 *
 * Decoding a data record means determining, for each data field, 
 *
 *   - if the data's endianness must be converted;
 *   - if the data needs to be transformed in any other way (for
 *     example, boolean values are encoded with 1 meaning true and 2
 *     meaning false(!!), or reduced-length encoding of floating-point
 *     values means that doubles are really transferred as floats);
 *   - for variable-length data, what the length of the encoded value
 *     is; and
 *   - where the converted data is to be stored.
 *
 * Basically, clients register sets of pairs of <ie, pointer> with the
 * IPFIXContentHandler class below.  This is called a Placement Template.
 * This placement template will then be used to match incoming data
 * records.  The previously used procedure was to nominate the first
 * placement template whose set of information elements is a subset of
 * the information elements for the data set in question. We
 * implement this here as well, but it might be changed easily.  (For
 * example, we might reasonably select that placement template that is a
 * subset of the data set's template and at the same time matches the
 * most fields.)
 *
 * Now having a template for the data set (called a Wire Template) and
 * a matching placement template, we create a Decoding Plan.  Basically,
 * a decoding plan is a sequence of decisions, one for each field.
 * There are two types of decisions:
 *
 *   - A SKIP decision causes the corresponding field to be skipped.
 *   - A TRANSFER decision causes the corresponding field to be
 *     endianness-converted if necessary (this can be gleaned form the
 *     corresponding information element's type), and copied to the
 *     pointer that the client gave at the time of registration.
 *
 * For convenience, there exist two variations of each decision,
 * depending on whether the corresponding field is fixed-length field
 * or a variable-length field.
 *
 * It would be nice if we could collapse two adjacent SKIP decisions
 * into one, but that can only be done if the two fields in question
 * are fixed-length fields.  We might do this in a future release if
 * it turns out to be a performance problem.
 */
class DecodePlan {
public:
  /** Creates a decoding plan from a placement template and a wire
   * template.
   *
   * @param placement_template a placement template that must have been
   *   found to match the wire template (all IEs in the placement
   *   template must also appear in the wire template)
   * @param wire_template the wire template for the data set
   */
  DecodePlan(const IPFIX::PlacementTemplate* placement_template,
             const IPFIX::MatchTemplate* wire_template);

  /** Executes the plan.
   *
   * Due to the construction of IPFIX (there exists variable-length
   * data and a data record does not have a header containing its
   * length), we do not know the exact length of that data record.
   * Hence we give the length of the remaining data set and expect
   * this member function to return the number of bytes that it has
   * decoded.
   *
   * @param buf the buffer containing the data record (and the
   *     remaining data set)
   * @param length length of the remaining data set
   *
   * @return number of bytes decoded
   */
  uint16_t execute(const uint8_t* buf, uint16_t length);

private:
  struct Decision {
    /** The decision type. */
    enum decision_type_t {
      /** Skip a fixed amount. */
      skip_fixlen,

      /** Skip a variable amount. */
      skip_varlen,

      /** Transfer a fixed amount, with no endianness conversion, no
       * booleans, and no octet string. */
      transfer_fixlen,

      /** Transfer a boolean. Someone found it amusing in RFC 2579 to
       * encode the boolean values true and false as 1 and 2,
       * respectively [sic!].  And someone else found it amusing to
       * standardise this behaviour in RFC 5101 too.  This is of
       * course wrong, since it disallows entirely sensible operations
       * like `plus' for "or", `times' for "and" and `less than' for
       * implication (which is what you get when you make false less
       * than true). */
      transfer_boolean,

      /** Transfer a fixed amount, with endianness conversion. */
      transfer_fixlen_endianness,

      /** Transfer an octet string with fixed length. */
      transfer_fixlen_octets,

      /** Reduced-length float64, no endianness conversion. */
      transfer_float_into_double,

      /** Reduced-length float64, with endianness conversion. */
      transfer_float_into_double_endianness,

      /** Transfer a variable amount. */
      transfer_varlen,
    } type;

    /** How much data is affected in the data set?  This field makes
     * sense only in fixlen decisions. */
    uint16_t length;

    /** Destination type size in bytes.  This field makes sense only in
     * transfer_fixlen decisions. */
    uint16_t destination_size;

    /** Transfer target. This field makes sense only in transfer
     * decisions.  The caller must make sure that these pointers are
     * suitably aligned for the result data type (for fixlen
     * transfers), or that they point to a BasicOctetArray object (for
     * varlen transfers). */
    void* p;

    /** Original wire template IE. This field makes sense only in
     * transfer decisions. */
    const IPFIX::InfoElement* wire_ie;

    std::string to_string() const;
  };
  
  std::vector<Decision> plan;

#ifdef _LIBFC_HAVE_LOG4CPLUS_
    log4cplus::Logger logger;
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */
};


std::string DecodePlan::Decision::to_string() const {
  std::stringstream sstr;

  sstr << "[";
  switch (type) {
  case skip_fixlen:
    sstr << "skip_fixlen " << length; break;
  case skip_varlen:
    sstr << "skip_varlen"; break;
  case transfer_fixlen:
    sstr << "transfer_fixlen " << length 
         << "/" << destination_size; break;
  case transfer_boolean:
    sstr << "transfer_boolean"; break;
  case transfer_fixlen_endianness:
    sstr << "transfer_fixlen_endianness " << length
         << "/" << destination_size; break;
  case transfer_fixlen_octets:
    sstr << "transfer_fixlen_octets " << length; break;
  case transfer_float_into_double:
    sstr << "transfer_float_into_double"; break;
  case transfer_float_into_double_endianness:
    sstr << "transfer_float_into_double_endianness"; break;
  case transfer_varlen:
    sstr << "transfer_varlen"; break;
  };
  sstr << "]";
  
  return sstr.str();
}


static void report_error(const char* message, ...) {
  static const size_t buf_size = 10240;
  static char buf[buf_size];
  va_list args;
  
  va_start(args, message);
  int nchars = vsnprintf(buf, buf_size, message, args);
  va_end(args);

  if (nchars < 0)
    strcpy(buf, "Error while formatting error message");
  else if (static_cast<unsigned int>(nchars) > buf_size - 1 - 3) {
    buf[buf_size - 4] = '.';
    buf[buf_size - 3] = '.';
    buf[buf_size - 2] = '.';
    buf[buf_size - 1] = '\0';   // Shouldn't be necessary
  }

  throw IPFIX::FormatError(buf);
}

DecodePlan::DecodePlan(const IPFIX::PlacementTemplate* placement_template,
                       const IPFIX::MatchTemplate* wire_template) 
  : plan(wire_template->size())
#ifdef _LIBFC_HAVE_LOG4CPLUS_
                               ,
    logger(log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("DecodePlan")))
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */
  {

  LOG4CPLUS_TRACE(logger, "ENTER DecodePlan::DecodePlan (wt with "
                  << wire_template->size() << " entries)");

#if defined(IPFIX_BIG_ENDIAN)
  Decision::decision_type_t transfer_fixlen_maybe_endianness
    = Decision::transfer_fixlen;
  Decision::decision_type_t transfer_float_into_double_maybe_endianness
    = Decision::transfer_float_into_double;
#elif defined(IPFIX_LITTLE_ENDIAN)
  Decision::decision_type_t transfer_fixlen_maybe_endianness
    = Decision::transfer_fixlen_endianness;
  Decision::decision_type_t transfer_float_into_double_maybe_endianness
    = Decision::transfer_float_into_double_endianness;
#else
#  error libfc does not compile on weird-endian machines.
#endif

  unsigned int decision_number = 0;
  for (auto ie = wire_template->begin(); ie != wire_template->end(); ie++) {
    assert(*ie != 0);
    LOG4CPLUS_TRACE(logger, "  decision " << (decision_number + 1)
                    << ": looking up placement");// for " << (*ie)->toIESpec());

    Decision d;

    if (placement_template->lookup_placement(*ie, &d.p, 0)) { /* IE present */
      LOG4CPLUS_TRACE(logger, "    found -> transfer");
      d.wire_ie = *ie;

      if ((*ie)->ietype() == 0)
        report_error("IE %s has NULL ietype", (*ie)->toIESpec().c_str());

      /* There is some code duplication going on here, but unless
       * someone can demonstrate to me that this leads to higher
       * maintenance costs, I'd like to keep each IE type separate,
       * until this code has been fully debugged. */
      switch ((*ie)->ietype()->number()) {
      case IPFIX::IEType::kOctetArray: 
        if ((*ie)->len() == IPFIX::kVarlen) {
          d.type = Decision::transfer_varlen;
        } else {
          d.type = Decision::transfer_fixlen_octets;
          d.length = (*ie)->len();
        }
        break;

      case IPFIX::IEType::kUnsigned8:
        d.type = Decision::transfer_fixlen;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint8_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kUnsigned16:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint16_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kUnsigned32:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint32_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kUnsigned64:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint64_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kSigned8:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(int8_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kSigned16:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(int16_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kSigned32:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(int32_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kSigned64:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(int64_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kFloat32:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(float);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kFloat64:
        assert((*ie)->len() == sizeof(float)
               || d.length == sizeof(double));
        d.length = (*ie)->len();
        if (d.length == sizeof(float))
          d.type = transfer_float_into_double_maybe_endianness;
        else
          d.type = transfer_fixlen_maybe_endianness;
        d.destination_size = sizeof(double);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kBoolean:
        d.type = Decision::transfer_boolean;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint8_t); 
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;

      case IPFIX::IEType::kMacAddress:
        /* RFC 5101 says to treat MAC addresses as 6-byte integers,
         * but Brian Trammell says that this is wrong and that the
         * RFC will be changed.  If for some reason this does not
         * come about, replace "transfer_fixlen" with
         * "transfer_fixlen_maybe_endianness". */
        d.type = Decision::transfer_fixlen;
        d.length = (*ie)->len();
        d.destination_size = 6*sizeof(uint8_t);
        if (d.length != 6)
          report_error("MAC IE not 6 octets long (c.f. RFC 5101, Chapter 6, Verse 2");
        break;
        
      case IPFIX::IEType::kString:
        if ((*ie)->len() == IPFIX::kVarlen) {
          d.type = Decision::transfer_varlen;
        } else {
          d.type = Decision::transfer_fixlen_octets;
          d.length = (*ie)->len();
        }
        break;

      case IPFIX::IEType::kDateTimeSeconds:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint32_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;
        
      case IPFIX::IEType::kDateTimeMilliseconds:
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint64_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;
        
      case IPFIX::IEType::kDateTimeMicroseconds:
        d.type = transfer_fixlen_maybe_endianness;
        // RFC 5101, Chapter 6, Verse 2
        assert((*ie)->len() == sizeof(uint64_t));
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint64_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;
        
      case IPFIX::IEType::kDateTimeNanoseconds:
        d.type = transfer_fixlen_maybe_endianness;
        // RFC 5101, Chapter 6, Verse 2
        assert((*ie)->len() == sizeof(uint64_t));
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint64_t);
        if (d.length > d.destination_size)
          report_error("IE %s length %zu greater than native size %zu",
                       (*ie)->toIESpec().c_str(), d.length, d.destination_size);
        break;
        
      case IPFIX::IEType::kIpv4Address:
        /* RFC 5101 says to treat all addresses as integers. This
         * would mean endianness conversion for all of these address
         * types, including MAC addresses and IPv6 addresses. But the
         * only reasonable address type with endianness conversion is
         * the IPv4 address.  If for some reason this is not correct
         * replace "transfer_fixlen_maybe_endianness" with
         * "transfer_fixlen". */
        d.type = transfer_fixlen_maybe_endianness;
        d.length = (*ie)->len();
        d.destination_size = sizeof(uint32_t);
        if (d.length != 4)
          report_error("IPv4 Address IE not 4 octets long (c.f. RFC 5101, Chapter 6, Verse 2");
        break;
        
      case IPFIX::IEType::kIpv6Address:
        /* RFC 5101 says to treat IPv6 addresses as 16-byte integers,
         * but Brian Trammell says that this is wrong and that the
         * RFC will be changed.  If for some reason this does not
         * come about, replace "transfer_fixlen" with
         * "transfer_fixlen_maybe_endianness". */
        d.type = Decision::transfer_fixlen;
        d.length = (*ie)->len();
        d.destination_size = 16*sizeof(uint8_t);
        if (d.length != 16)
          report_error("IPv6 Address IE not 16 octets long (c.f. RFC 5101, Chapter 6, Verse 2");
        break;
        
      default: 
        report_error("Unknown IE type");
        break;
      }
    } else {                    /* Encode skip decision */
      LOG4CPLUS_TRACE(logger, "    not found -> skip");
      if ((*ie)->len() == IPFIX::kVarlen) {
        d.type = Decision::skip_varlen;
      } else {
        d.type = Decision::skip_fixlen;
        d.length = (*ie)->len();
      }
    }

    plan[decision_number++] = d;
    LOG4CPLUS_TRACE(logger, "  decision " << decision_number
                    << " entered as " << d.to_string());
  }

  /* Coalesce adjacent skip_fixlen decisions. */
  for (auto decision = plan.begin(); decision != plan.end(); ++decision) {
    if (decision->type == Decision::skip_fixlen) {
      auto skips = decision;
      auto next = decision;
      uint16_t length = decision->length;
      
      ++next;
      for (++skips;
           skips != plan.end() && skips->type == Decision::skip_fixlen;
           ++skips)
        length += skips->length;
      plan.erase(next, skips);
      decision->length = length;
    }
  }

#ifdef _LIBFC_HAVE_LOG4CPLUS_
  if (logger.getLogLevel() <= log4cplus::DEBUG_LOG_LEVEL) {
    LOG4CPLUS_TRACE(logger, "  plan is: ");
    for (auto d = plan.begin(); d != plan.end(); ++d)
      LOG4CPLUS_TRACE(logger, "    " << d->to_string());
  }
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */

  LOG4CPLUS_TRACE(logger, "LEAVE DecodePlan::DecodePlan");
}

static uint16_t decode_varlen_length(const uint8_t** cur,
                                     const uint8_t* buf_end) {
  uint16_t ret = 0;

  if (*cur >= buf_end) 
    report_error("first octet of varlen length encoding beyond buffer");
  ret = **cur;

  if (ret < UCHAR_MAX)
    (*cur)++;
  else {
    if (*cur + 3 > buf_end) 
      report_error("three-byte varlen length encoding beyond buffer");
    (*cur)++;
    /* Assume that the two length-carrying octets are in network byte
     * order */
    ret = (*(*cur + 0) << 8) + *(*cur + 1);
    *cur += 2;

    /* If it turns out that the three-byte encoding must not be used
     * for values < 255, then uncomment the following two lines: */
    //if (ret < UCHAR_MAX)
    //  report_error("three-byte varlen encoding used for value < 255");
  }

  if (*cur + ret > buf_end) {
    std::stringstream sstr;
    sstr << "varlen length " << ret
         << " (0x" << std::hex << ret << ")" << std::dec
         << " goes beyond buffer (only " << (buf_end - *cur)
         << " bytes left";
    report_error(sstr.str().c_str());
  }

  return ret;
}

#if defined(_LIBFC_HAVE_LOG4CPLUS_) && defined(_LIBFC_DO_HEXDUMP_)
static void hexdump(log4cplus::Logger& logger, const uint8_t* buf,
                    const uint8_t* buf_end) {
  char out_buf[81];
  const uint8_t* p = buf;
  size_t size = sizeof(out_buf);
  for (; p < buf_end; p += 16) {
    snprintf(out_buf, size, "%08x", static_cast<unsigned int>(p - buf));
    size -= 8;
    for (int i = 0; i < 16 && p < buf_end; i++) {
      snprintf(out_buf + 8 + 3*i, size, " %02x", p[i]);
      size -= 3;
    }
    LOG4CPLUS_TRACE(logger, out_buf);
  }
}
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */

uint16_t DecodePlan::execute(const uint8_t* buf, uint16_t length) {
  LOG4CPLUS_TRACE(logger, "ENTER DecodePlan::execute");

  const uint8_t* cur = buf;
  const uint8_t* buf_end = buf + length;

  for (auto i = plan.begin(); i != plan.end(); ++i) {
    assert(cur < buf_end);

    LOG4CPLUS_TRACE(logger, "  decision: " << i->type);

    switch (i->type) {
    case Decision::skip_fixlen:
      assert (cur + i->length <= buf_end);
      cur += i->length;
      break;

    case Decision::skip_varlen:
      {
        uint16_t varlen_length = decode_varlen_length(&cur, buf_end);
        assert(cur + varlen_length <= buf_end);
        cur += varlen_length;
      }
      break;

    case Decision::transfer_boolean:
      assert(cur + 1 <= buf_end);
      // Undo RFC 2579 madness
      {
        bool *q = static_cast<bool*>(i->p);
        if (*cur == 1)
          *q = 1;
        else if (*cur == 2)
          *q = 0;
        else
          report_error("bool encoding wrong");
      }
      cur++;
      break;

    case Decision::transfer_fixlen:
      if (cur + i->length > buf_end)
        report_error("IE %s length beyond buffer: cur=%p, ielen=%zu, end=%p",
                     i->wire_ie->toIESpec().c_str(), cur, i->length, buf_end);

      assert(i->length <= i->destination_size);

      /* Assume all-zero bit pattern is zero, null, 0.0 etc. */
      // FIXME: Check if transferring native data types is faster
      // (e.g., short when i->length == 2, long when i->length == 4
      // etc).
      {
        uint8_t* q = static_cast<uint8_t*>(i->p);
        LOG4CPLUS_TRACE(logger, "  fixlen: q == " << static_cast<void*>(q));

        memset(q, '\0', i->destination_size);
        // Intention: right-justify value at cur in field at i->p
        memcpy(q + i->destination_size - i->length, cur, i->length);
      }
      cur += i->length;
      break;

    case Decision::transfer_fixlen_endianness:
      if (cur + i->length > buf_end)
        report_error("IE %s length beyond buffer: cur=%p, ielen=%zu, end=%p",
                     i->wire_ie->toIESpec().c_str(), cur, i->length, buf_end);

      assert(i->length <= i->destination_size);

#if defined(_LIBFC_HAVE_LOG4CPLUS_) && defined(_LIBFC_DO_HEXDUMP_)
        {
          LOG4CPLUS_TRACE(logger, "transfer w/endianness Before");
          hexdump(logger, buf, cur);
          LOG4CPLUS_TRACE(logger, "At and after");
          hexdump(logger, cur, cur + 8);
        }
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */

      /* Assume all-zero bit pattern is zero, null, 0.0 etc. */
      // FIXME: Check if transferring native data types is faster
      // (e.g., short when i->length == 2, long when i->length == 4
      // etc).
      {
        uint8_t* q = static_cast<uint8_t*>(i->p);        
        LOG4CPLUS_TRACE(logger, "  fixlen_endianness: q == " << static_cast<void*>(q) << ", size=" << i->destination_size);
        memset(q, '\0', i->destination_size);
        LOG4CPLUS_TRACE(logger, "  memset done");
        // Intention: left-justify value at cur in field at i->p
        for (uint16_t k = 0; k < i->length; k++)
          q[k] = cur[i->length - (k + 1)];
      }
      LOG4CPLUS_TRACE(logger, "  transfer done");
      cur += i->length;
      break;

    case Decision::transfer_fixlen_octets:
      assert(cur + i->length <= buf_end);
      { 
        IPFIX::BasicOctetArray* p
          = reinterpret_cast<IPFIX::BasicOctetArray*>(i->p);
        p->copy_content(cur, i->length);
      }

      cur += i->length;
      break;

    case Decision::transfer_float_into_double:
      assert(cur + sizeof(float) <= buf_end);
      {
        float f;
        memcpy(&f, cur, sizeof(float));
        *static_cast<double*>(i->p) = f;
      }
      cur += sizeof(float);
      break;

    case Decision::transfer_float_into_double_endianness:
      assert(cur + sizeof(float) <= buf_end);
      {
        union {
          uint8_t b[sizeof(float)];
          float f;
        } val;
        val.b[0] = cur[3];
        val.b[1] = cur[2];
        val.b[2] = cur[1];
        val.b[3] = cur[0];
        *static_cast<double*>(i->p) = val.f;
      }
      cur += sizeof(uint32_t);
      break;

    case Decision::transfer_varlen:
      {
#if defined(_LIBFC_HAVE_LOG4CPLUS_) && defined(_LIBFC_DO_HEXDUMP_)
        {
          LOG4CPLUS_TRACE(logger, "Before");
          hexdump(logger, buf, cur);
          LOG4CPLUS_TRACE(logger, "At and after");
          hexdump(logger, cur, std::min(cur + 12, buf_end));
        }
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
        uint16_t varlen_length = decode_varlen_length(&cur, buf_end);
        LOG4CPLUS_TRACE(logger, "  varlen length " << varlen_length);
        assert(cur + varlen_length <= buf_end);
      
        IPFIX::BasicOctetArray* p
          = reinterpret_cast<IPFIX::BasicOctetArray*>(i->p);
        p->copy_content(cur, varlen_length);

        cur += varlen_length;
      }
      break;
    }
  }

  assert ((cur - buf) <= USHRT_MAX);
  return static_cast<uint16_t>(cur - buf);
}

namespace IPFIX {

  IPFIXContentHandler::IPFIXContentHandler()
    : info_model(InfoModel::instance()),
      current_wire_template(0),
      parse_is_good(true)
#ifdef _LIBFC_HAVE_LOG4CPLUS_
                         ,
      logger(log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("IPFIXContentHandler")))
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */
  {
  }

  IPFIXContentHandler::~IPFIXContentHandler() {
    if (parse_is_good) { /* Check assertions only when no exception. */
      assert (current_wire_template == 0);
    }
  }

#ifdef _LIBFC_HAVE_LOG4CPLUS_
  static const char* make_time(uint32_t export_time) {
    struct tm tms;
    time_t then = export_time;
    static char gmtime_buf[100];

    gmtime_r(&then, &tms);
    strftime(gmtime_buf, sizeof gmtime_buf, "%c", &tms);
    gmtime_buf[sizeof(gmtime_buf) - 1] = '\0';

    return gmtime_buf;
  }
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */

  void IPFIXContentHandler::start_session() {
    LOG4CPLUS_TRACE(logger, "Session starts");
  }

  void IPFIXContentHandler::end_session() {
    LOG4CPLUS_TRACE(logger, "Session ends");
  }

  void IPFIXContentHandler::start_message(uint16_t version,
                                     uint16_t length,
                                     uint32_t export_time,
                                     uint32_t sequence_number,
                                     uint32_t observation_domain,
				     uint64_t base_time) {
    LOG4CPLUS_TRACE(logger,
                    "ENTER start_message"
                    << ", version=" << version
                    << ", length=" << length
                    << ", export_time=" << make_time(export_time)
                    << ", sequence_number=" << sequence_number
                    << ", observation_domain=" << observation_domain
		    << ", base_time=" << base_time);
    assert(current_wire_template == 0);

    if (version != kIpfixVersion) {
      parse_is_good = false;
      report_error("Expected message version %04x, got %04x",
                   kIpfixVersion, version);
    }

    if (base_time != 0) {
      parse_is_good = false;
      report_error("Expected base_time 0, got %04x", base_time);
    }

    this->observation_domain = observation_domain;
    LOG4CPLUS_TRACE(logger, "LEAVE start_message");
  }

  void IPFIXContentHandler::end_message() {
    LOG4CPLUS_TRACE(logger, "ENTER end_message");
    assert(current_wire_template == 0);
    LOG4CPLUS_TRACE(logger, "LEAVE end_message");
  }

  void IPFIXContentHandler::start_template_set(uint16_t set_id,
					       uint16_t set_length,
					       const uint8_t* buf) {
    LOG4CPLUS_TRACE(logger, "ENTER start_template_set"
                    << ", set_id=" << set_id
                    << ", set_length=" << set_length);
    assert(current_wire_template == 0);

    const uint8_t* set_end = buf + set_length;

    while (buf + kTemplateHeaderLen <= set_end) {
      /* Decode template record */
      uint16_t set_id = decode_uint16(buf + 0);
      uint16_t field_count = decode_uint16(buf + 2);
      
      start_template_record(set_id, field_count);
      
      buf += kTemplateHeaderLen;
      
      for (unsigned int field = 0; field < field_count; field++) {
	if (buf + kFieldSpecifierLen > set_end) {
	  error(Error::long_fieldspec, 0);
	  return;
	}
	
	uint16_t ie_id = decode_uint16(buf + 0);
	uint16_t ie_length = decode_uint16(buf + 2);
	bool enterprise = ie_id & 0x8000;
	ie_id &= 0x7fff;
	
	uint32_t enterprise_number = 0;
	if (enterprise) {
	  if (buf + kFieldSpecifierLen + kEnterpriseLen > set_end) {
	    error(Error::long_fieldspec, 0);
	    return;
	  }
	  enterprise_number = decode_uint32(buf + 4);
	}
	
	field_specifier(enterprise, ie_id, ie_length, enterprise_number);
	
	buf += kFieldSpecifierLen + (enterprise ? kEnterpriseLen : 0);
	assert (buf <= set_end);
      }
      
      end_template_record();
    }
  }

  void IPFIXContentHandler::end_template_set() {
    LOG4CPLUS_TRACE(logger, "ENTER end_template_set");
  }

  uint64_t IPFIXContentHandler::make_template_key(uint16_t tid) const {
    return (static_cast<uint64_t>(observation_domain) << 16) + tid;
  }

  void IPFIXContentHandler::start_template_record(
      uint16_t template_id,
      uint16_t field_count) {
    LOG4CPLUS_TRACE(logger,
                    "ENTER start_template_record"
                    << ", template_id=" << template_id
                    << ", field_count=" << field_count);
    assert(current_wire_template == 0);
    current_template_id = template_id;

    /* It is not an error if the same template (as given by template
     * ID and observation domain) is repeated, so we don't check for
     * that. FIXME this has changed! --neuhaust */
    current_field_count = field_count;
    current_field_no = 0;
    current_wire_template = new MatchTemplate();
  }

  void IPFIXContentHandler::end_template_record() {
    LOG4CPLUS_TRACE(logger, "ENTER end_template_record");
    if (current_wire_template->size() > 0) {
#if defined(_LIBFC_HAVE_LOG4CPLUS_)
      {
        std::map<uint64_t, const MatchTemplate*>::const_iterator i
          = wire_templates.find(make_template_key(current_template_id));
        if (i != wire_templates.end())
          LOG4CPLUS_TRACE(logger, "  overwriting template for id "
                          << std::hex
                          << make_template_key(current_template_id));
      }

#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */

      wire_templates[make_template_key(current_template_id)]
        = current_wire_template;

#if defined(_LIBFC_HAVE_LOG4CPLUS_)
      if (logger.getLogLevel() <= log4cplus::DEBUG_LOG_LEVEL) {
        LOG4CPLUS_TRACE(logger,
                        "  current wire template has "
                        << current_wire_template->size()
                        << " entries, there are now "
                        << wire_templates.size()
                        << " registered wire templates");
        unsigned int n = 1;
        for (auto i = current_wire_template->begin(); i != current_wire_template->end(); i++)
          LOG4CPLUS_TRACE(logger, "  " << n++ << " " << (*i)->toIESpec().c_str());
      }
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
    }

    if (current_field_count != current_field_no) {
      parse_is_good = false;
      report_error("Template field mismatch: expected %u fields, got %u",
                   current_field_count, current_field_no);
    }

    current_wire_template = 0;
  }

  void IPFIXContentHandler::start_option_template_set(
      uint16_t set_id,
      uint16_t set_length,
      const uint8_t* buf) {
    LOG4CPLUS_TRACE(logger, "ENTER start_option_template_set"
                    << ", set_id=" << set_id
                    << ", set_length=" << set_length);
    assert(current_wire_template == 0);
  }

  void IPFIXContentHandler::end_option_template_set() {
    LOG4CPLUS_TRACE(logger, "ENTER end_option_template_set");
  }

  void IPFIXContentHandler::start_option_template_record(
      uint16_t template_id,
      uint16_t field_count,
      uint16_t scope_field_count) {
    LOG4CPLUS_TRACE(logger, "ENTER start_option_template_record"
                    << ", template_id=" << template_id
                    << ", field_count=" << field_count
                    << ", scope_field_count=" << scope_field_count);
    assert(current_wire_template == 0);
  }

  void IPFIXContentHandler::end_option_template_record() {
    LOG4CPLUS_TRACE(logger, "ENTER end_option_template_record");
  }

  void IPFIXContentHandler::field_specifier(
      bool enterprise,
      uint16_t ie_id,
      uint16_t ie_length,
      uint32_t enterprise_number) {
    LOG4CPLUS_TRACE(logger,
                    "ENTER field_specifier"
                    << ", enterprise=" << enterprise
                    << ", pen=" << enterprise_number
                    << ", ie=" << ie_id
                    << ", length=" << ie_length);
    
    if (current_field_no >= current_field_count) {
      parse_is_good = false;
      report_error("Template contains more field specifiers than were "
                   "given in the header");
    }

    LOG4CPLUS_TRACE(logger, "  looking up (" << enterprise_number
                    << "/" << ie_id
                    << ")[" << ie_length << "]");
    const InfoElement* ie
      = info_model.lookupIE(enterprise_number, ie_id, ie_length);

    assert(enterprise || enterprise_number == 0);
    assert ((enterprise && enterprise_number != 0) || ie != 0);

    if (ie == 0) {
      if (enterprise)
        LOG4CPLUS_TRACE(logger,
                        "  if unknown, enter "
                        << " (" << enterprise_number
                        << "/" << ie_id
                        << ")<sometype>[" << ie_length
                        << "]");

      ie = info_model.add_unknown(enterprise_number, ie_id, ie_length);
    }

    assert(ie != 0);

    LOG4CPLUS_TRACE(logger, "  found " << (current_field_no + 1)
                    << ": " << ie->toIESpec());

    current_wire_template->add(ie);
    current_field_no++;
  }

  const MatchTemplate*
  IPFIXContentHandler::find_wire_template(uint16_t id) const {
    std::map<uint64_t, const MatchTemplate*>::const_iterator i
      = wire_templates.find(make_template_key(id));
    return i == wire_templates.end() ? 0 : i->second;
  }

  const PlacementTemplate*
  IPFIXContentHandler::match_placement_template(const MatchTemplate* wire_template) const {
    LOG4CPLUS_TRACE(logger, "ENTER match_placement_template");

    /* This strategy: return first match. Other strategies are also
     * possible, such as "return match with most IEs". */
#if defined(LIBFC_USE_MATCHED_TEMPLATE_CACHE)
    std::map<const MatchTemplate*, const PlacementTemplate*>::const_iterator m
      = matched_templates.find(wire_template);

    if (m == matched_templates.end()) {
      for (auto i = placement_templates.begin();
           i != placement_templates.end();
           ++i) {
        if ((*i)->is_match(wire_template)) {
          matched_templates[wire_template] = *i;
          return *i;
        }
      }
      return 0;
    } else
      return m->second;
#else /* !defined(LIBFC_USE_MATCHED_TEMPLATE_CACHE) */
    for (auto i = placement_templates.begin();
         i != placement_templates.end();
         ++i) {
      if ((*i)->is_match(wire_template))
        return *i;
    }
    return 0;
#endif /* defined(LIBFC_USE_MATCHED_TEMPLATE_CACHE) */
  }

  void IPFIXContentHandler::start_data_set(uint16_t id,
                                      uint16_t length,
                                      const uint8_t* buf) {
    LOG4CPLUS_TRACE(logger,
                    "ENTER start_data_set"
                    << ", id=" << id
                    << ", length=" << length);

    // Find out who is interested in data from this data set
    const MatchTemplate* wire_template = find_wire_template(id);

    LOG4CPLUS_TRACE(logger, "  wire_template=" << wire_template);

    if (wire_template == 0) {
      LOG4CPLUS_TRACE(logger, "  no template for this data set; skipping");
      return;
    }

    const PlacementTemplate* placement_template
      = match_placement_template(wire_template);

    LOG4CPLUS_TRACE(logger, "  placement_template=" << placement_template);

    if (placement_template == 0) {
      LOG4CPLUS_TRACE(logger, "  no one interested in this data set; skipping");
      return;
    }

    DecodePlan plan(placement_template, wire_template);

    const uint8_t* buf_end = buf + length;
    const uint8_t* cur = buf;
    const uint16_t min_length = wire_template_min_length(wire_template);
    
    auto callback = callbacks.find(placement_template);
    assert(callback != callbacks.end());

    while (cur < buf_end && length >= min_length) {
      callback->second->start_placement(placement_template);
      uint16_t consumed = plan.execute(cur, length);
      callback->second->end_placement(placement_template);
      cur += consumed;
      length -= consumed;
    }

  }

  void IPFIXContentHandler::end_data_set() {
    LOG4CPLUS_TRACE(logger, "ENTER end_data_set");
    LOG4CPLUS_TRACE(logger, "LEAVE end_data_set");
  }

  void IPFIXContentHandler::error(Error error, const char* message) {
    LOG4CPLUS_TRACE(logger, "Warning: " << error << ": " << message);
  }

  void IPFIXContentHandler::fatal(Error error, const char* message) {
    LOG4CPLUS_TRACE(logger, "Warning: " << error << ": " << message);
  }

  void IPFIXContentHandler::warning(Error error, const char* message) {
    LOG4CPLUS_TRACE(logger, "Warning: " << error << ": " << message);
  }

  void IPFIXContentHandler::register_placement_template(
      const PlacementTemplate* placement_template,
      PlacementCollector* callback) {
    placement_templates.push_back(placement_template);
    callbacks[placement_template] = callback;
  }

  uint16_t IPFIXContentHandler::wire_template_min_length(const MatchTemplate* t) {
    uint16_t min = 0;

    for (auto i = t->begin(); i != t->end(); ++i) {
      if ((*i)->len() != kVarlen)
        min += (*i)->len();
    }
    return min;
  }

} // namespace IPFIX