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
#include <iomanip>
#include <sstream>

#include <time.h>

#ifdef _LIBFC_HAVE_LOG4CPLUS_
#include <log4cplus/loggingmacros.h>
#else
#define LOG4CPLUS_TRACE(logger, expr)
#define LOG4CPLUS_WARN(logger, expr)
#define LOG4CPLUS_INFO(logger, expr)
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */

#include "decode_util.h"
#include "pointer_checks.h"

#include "BasicOctetArray.h"
#include "DecodePlan.h"
#include "PlacementCollector.h"
#include "PlacementContentHandler.h"

namespace libfc {

#define CH_REPORT_ERROR(error, message_stream)                                 \
  do {                                                                         \
    parse_is_good = false;                                                     \
    LIBFC_RETURN_ERROR(recoverable, error, message_stream, 0, 0, 0, 0, 0);     \
  } while (0)

#define CH_REPORT_CALLBACK_ERROR(call)                                         \
  do {                                                                         \
    /* Make sure call is evaluated only once */                                \
    std::shared_ptr<ErrorContext> err = call;                                  \
    if (err != 0)                                                              \
      return err;                                                              \
  } while (0)

PlacementContentHandler::PlacementContentHandler()
    : info_model(InfoModel::instance()), start_message_handler(0),
      unhandled_data_set_handler(0), use_matched_template_cache(false),
      current_wire_template(0), parse_is_good(true)
#ifdef _LIBFC_HAVE_LOG4CPLUS_
      ,
      logger(log4cplus::Logger::getInstance(
          LOG4CPLUS_TEXT("PlacementContentHandler")))
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */
{
}

PlacementContentHandler::~PlacementContentHandler() {
  if (parse_is_good) { /* Check assertions only when no exception. */
    assert(current_wire_template == 0);
  }

  for (auto i = wire_templates.begin(); i != wire_templates.end(); ++i)
    delete i->second;
}

#ifdef _LIBFC_HAVE_LOG4CPLUS_
static const char *make_time(uint32_t export_time) {
  struct tm tms;
  time_t then = export_time;
  static char gmtime_buf[100];

  gmtime_r(&then, &tms);
  strftime(gmtime_buf, sizeof gmtime_buf, "%c", &tms);
  gmtime_buf[sizeof(gmtime_buf) - 1] = '\0';

  return gmtime_buf;
}
#endif /* _LIBFC_HAVE_LOG4CPLUS_ */

std::shared_ptr<ErrorContext> PlacementContentHandler::start_session() {
  LOG4CPLUS_TRACE(logger, "Session starts");
  return std::shared_ptr<ErrorContext>(0);
}

std::shared_ptr<ErrorContext> PlacementContentHandler::end_session() {
  LOG4CPLUS_TRACE(logger, "Session ends");
  return std::shared_ptr<ErrorContext>(0);
}

std::shared_ptr<ErrorContext> PlacementContentHandler::start_message(
    uint16_t version, uint16_t length, uint32_t export_time,
    uint32_t sequence_number, uint32_t observation_domain, uint64_t base_time) {
  LOG4CPLUS_TRACE(logger, "ENTER start_message"
                              << ", version=" << version
                              << ", length=" << length
                              << ", export_time=" << make_time(export_time)
                              << ", sequence_number=" << sequence_number
                              << ", observation_domain=" << observation_domain
                              << ", base_time=" << base_time);
  assert(current_wire_template == 0);

  /* At this point, we can be sure that the version is correct for
   * the underlying MessageStreamParser class.  In other words, if
   * the MessageStreamParser that calls this method is an
   * IPFIXMessageStreamParser, then version will be equal to 10, and
   * so on.  But still, some things don't make sense for IPFIX, for
   * example a nonzero base time. */
  if (version == kIpfixVersion && base_time != 0)
    CH_REPORT_ERROR(ipfix_basetime, "Expected base_time 0 for IPFIX, got 0x"
                                        << std::hex << std::setw(4)
                                        << base_time);

  /* TODO: Figure out (and check for) minimal message lengths for v9
   * and v5. */
  if (version == kIpfixVersion && length < kIpfixMinMessageLen)
    CH_REPORT_ERROR(short_message, "must be at least "
                                       << kIpfixMinMessageLen
                                       << " bytes long, got only " << length);

  this->observation_domain = observation_domain;

  LOG4CPLUS_TRACE(logger, "LEAVE start_message");

  if (start_message_handler != 0)
    return start_message_handler->start_message(version, length, export_time,
                                                sequence_number,
                                                observation_domain, base_time);
  else
    LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::end_message() {
  LOG4CPLUS_TRACE(logger, "ENTER end_message");
  assert(current_wire_template == 0);
  LOG4CPLUS_TRACE(logger, "LEAVE end_message");
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::process_template_set(
    uint16_t set_id, uint16_t set_length, const uint8_t *buf,
    bool is_options_set) {
  const uint8_t *cur = buf;
  const uint8_t *set_end = buf + set_length;
  const uint16_t header_length =
      is_options_set ? kOptionsTemplateHeaderLen : kTemplateHeaderLen;

  while (CHECK_POINTER_WITHIN_I(cur + header_length, cur, set_end)) {
    /* Decode template record */
    uint16_t set_id = decode_uint16(cur + 0);
    uint16_t field_count = decode_uint16(cur + 2);
    uint16_t scope_field_count = is_options_set ? decode_uint16(cur + 4) : 0;

    CH_REPORT_CALLBACK_ERROR(start_template_record(set_id, field_count));

    cur += header_length;

    for (unsigned int field = 0; field < field_count; field++) {
      if (!CHECK_POINTER_WITHIN_I(cur + kFieldSpecifierLen, cur, set_end)) {
        LIBFC_RETURN_ERROR(recoverable, long_fieldspec,
                           "Field specifier partly outside template record", 0,
                           0, 0, 0, cur - buf);
      }

      uint16_t ie_id = decode_uint16(cur + 0);
      uint16_t ie_length = decode_uint16(cur + 2);
      bool enterprise = ie_id & 0x8000;
      ie_id &= 0x7fff;

      uint32_t enterprise_number = 0;
      if (enterprise) {
        if (!CHECK_POINTER_WITHIN_I(cur + kFieldSpecifierLen + kEnterpriseLen,
                                    cur, set_end)) {
          LIBFC_RETURN_ERROR(recoverable, long_fieldspec,
                             "Field specifier partly outside template "
                             "record (enterprise)",
                             0, 0, 0, 0, cur - buf);
        }
        enterprise_number = decode_uint32(cur + 4);
      }

      if (is_options_set && field < scope_field_count)
        CH_REPORT_CALLBACK_ERROR(scope_field_specifier(
            enterprise, ie_id, ie_length, enterprise_number));
      else if (is_options_set)
        CH_REPORT_CALLBACK_ERROR(options_field_specifier(
            enterprise, ie_id, ie_length, enterprise_number));
      else /* !is_options_set */
        CH_REPORT_CALLBACK_ERROR(
            field_specifier(enterprise, ie_id, ie_length, enterprise_number));

      cur += kFieldSpecifierLen + (enterprise ? kEnterpriseLen : 0);
      assert(cur <= set_end);
    }

    CH_REPORT_CALLBACK_ERROR(end_template_record());
  }
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::start_template_set(
    uint16_t set_id, uint16_t set_length, const uint8_t *buf) {
  LOG4CPLUS_TRACE(logger, "ENTER start_template_set"
                              << ", set_id=" << set_id
                              << ", set_length=" << set_length);
  assert(current_wire_template == 0);

  process_template_set(set_id, set_length, buf, false);
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::end_template_set() {
  LOG4CPLUS_TRACE(logger, "ENTER end_template_set");
  LIBFC_RETURN_OK();
}

uint64_t PlacementContentHandler::make_template_key(uint16_t tid) const {
  return (static_cast<uint64_t>(observation_domain) << 16) + tid;
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::start_template_record(uint16_t template_id,
                                               uint16_t field_count) {
  LOG4CPLUS_TRACE(logger, "ENTER start_template_record"
                              << ", template_id=" << template_id
                              << ", field_count=" << field_count);
  assert(current_wire_template == 0);
  current_template_id = template_id;

  /* It is not an error if the same template (as given by template
   * ID and observation domain) is repeated, so we don't check for
   * that. FIXME this has changed! --neuhaust */
  current_field_count = field_count;
  current_field_no = 0;
  current_wire_template = new IETemplate();

  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::end_template_record() {
  LOG4CPLUS_TRACE(logger, "ENTER end_template_record");
  assert(current_wire_template != 0);

  if (current_wire_template->size() > 0) {

    const IETemplate *my_wire_template =
        find_wire_template(current_template_id);
    if (my_wire_template != 0 && *my_wire_template != *current_wire_template) {
      LOG4CPLUS_WARN(logger, "  Overwriting template for domain "
                                 << observation_domain << ", ID "
                                 << current_template_id);

      incomplete_template_ids.erase(current_template_id);

      delete wire_templates[make_template_key(current_template_id)];
      wire_templates[make_template_key(current_template_id)] =
          current_wire_template;

      matched_templates.erase(my_wire_template);
    } else if (my_wire_template == 0) {
      LOG4CPLUS_INFO(logger, "  New template for domain "
                                 << observation_domain << ", ID "
                                 << current_template_id);
      wire_templates[make_template_key(current_template_id)] =
          current_wire_template;
    } else {
      assert(my_wire_template != 0 &&
             *my_wire_template == *current_wire_template);
      LOG4CPLUS_TRACE(logger, "  Duplicate template for domain "
                                  << observation_domain << ", ID "
                                  << current_template_id);
    }

#if defined(_LIBFC_HAVE_LOG4CPLUS_)
    if (logger.getLogLevel() <= log4cplus::TRACE_LOG_LEVEL) {
      LOG4CPLUS_TRACE(logger, "  current wire template has "
                                  << current_wire_template->size()
                                  << " entries, there are now "
                                  << wire_templates.size()
                                  << " registered wire templates");
      unsigned int n = 1;
      for (auto i = current_wire_template->begin();
           i != current_wire_template->end(); i++)
        LOG4CPLUS_TRACE(logger, "  " << n++ << " " << (*i)->toIESpec());
    }
#endif /* defined(_LIBFC_HAVE_LOG4CPLUS_) */
  }

  if (current_field_count != current_field_no)
    CH_REPORT_ERROR(format_error, "Template field mismatch: expected "
                                      << current_field_count << " fields, got "
                                      << current_field_no);

  current_wire_template = 0;
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::start_options_template_set(uint16_t set_id,
                                                    uint16_t set_length,
                                                    const uint8_t *buf) {
  LOG4CPLUS_TRACE(logger, "ENTER start_options_template_set"
                              << ", set_id=" << set_id
                              << ", set_length=" << set_length);
  assert(current_wire_template == 0);

  process_template_set(set_id, set_length, buf, true);
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::end_options_template_set() {
  LOG4CPLUS_TRACE(logger, "ENTER end_option_template_set");
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::field_specifier(bool enterprise, uint16_t ie_id,
                                         uint16_t ie_length,
                                         uint32_t enterprise_number) {
  LOG4CPLUS_TRACE(logger, "ENTER field_specifier"
                              << ", enterprise=" << enterprise
                              << ", pen=" << enterprise_number
                              << ", ie=" << ie_id << ", length=" << ie_length);

  if (current_field_no >= current_field_count)
    CH_REPORT_ERROR(format_error,
                    "Template contains more field specifiers than were "
                    "given in the header");

  LOG4CPLUS_TRACE(logger, "  looking up (" << enterprise_number << "/" << ie_id
                                           << ")[" << ie_length << "]");
  const InfoElement *ie =
      info_model.lookupIE(enterprise_number, ie_id, ie_length);

  /* There used to be an assert here:
   *
   *   assert ((enterprise && enterprise_number != 0) || ie != 0);
   *
   * The idea was that it was OK if private IEs were not in the
   * information model, but "official" ones, i.e., those that did
   * not have the enterprise bit set, needed to be present.
   *
   * But it has now been decided that unknown IEs should be inserted
   * into the information model, no matter whether they have the
   * enterprise bit set or not.
   *
   * But it's still true that iff the enterprise bit is set, then
   * enterprise_number must be nonzero. So:
   */
  assert(enterprise || enterprise_number == 0);
  assert(!enterprise || enterprise_number != 0);

  if (ie == 0) {
    LOG4CPLUS_TRACE(logger,
                    "  IE (" << enterprise_number << "/" << ie_id
                             << ")<sometype>[" << ie_length
                             << "] unknown, entering into information model");
    ie = info_model.add_unknown(enterprise_number, ie_id, ie_length);
  }

  assert(ie != 0);

  LOG4CPLUS_TRACE(logger, "  found " << (current_field_no + 1) << ": "
                                     << ie->toIESpec());

  current_wire_template->add(ie);
  current_field_no++;
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::scope_field_specifier(bool enterprise, uint16_t ie_id,
                                               uint16_t ie_length,
                                               uint32_t enterprise_number) {
  LOG4CPLUS_TRACE(logger, "ENTER scope_field_specifier"
                              << ", enterprise=" << enterprise
                              << ", pen=" << enterprise_number
                              << ", ie=" << ie_id << ", length=" << ie_length);
  field_specifier(enterprise, ie_id, ie_length, enterprise_number);
  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::options_field_specifier(
    bool enterprise, uint16_t ie_id, uint16_t ie_length,
    uint32_t enterprise_number) {
  LOG4CPLUS_TRACE(logger, "ENTER options_field_specifier"
                              << ", enterprise=" << enterprise
                              << ", pen=" << enterprise_number
                              << ", ie=" << ie_id << ", length=" << ie_length);
  field_specifier(enterprise, ie_id, ie_length, enterprise_number);
  LIBFC_RETURN_OK();
}

const IETemplate *
PlacementContentHandler::find_wire_template(uint16_t id) const {
  std::map<uint64_t, const IETemplate *>::const_iterator i =
      wire_templates.find(make_template_key(id));
  return i == wire_templates.end() ? 0 : i->second;
}

const PlacementTemplate *PlacementContentHandler::match_placement_template(
    uint16_t id, const IETemplate *wire_template) const {
  LOG4CPLUS_TRACE(logger, "ENTER match_placement_template");

  /* This strategy: return first match. Other strategies are also
   * possible, such as "return match with most IEs". */
  std::map<const IETemplate *, const PlacementTemplate *>::const_iterator m;

  if (use_matched_template_cache)
    m = matched_templates.find(wire_template);
  else
    m = matched_templates.end();

  if (m == matched_templates.end()) {
    for (auto i = placement_templates.begin(); i != placement_templates.end();
         ++i) {
      std::set<const InfoElement *> *unmatched =
          new std::set<const InfoElement *>();

      unsigned int n_matches = (*i)->is_match(wire_template, unmatched);
      LOG4CPLUS_TRACE(logger, "n_matches=" << n_matches << ",unmatched->size()="
                                           << unmatched->size()
                                           << ",wire_template->size()="
                                           << wire_template->size());

      if (n_matches > 0) {
        assert(n_matches <= wire_template->size());

        if (n_matches < wire_template->size()) {
          /* We're losing columns, so let's warn about them. */
          assert(unmatched->size() == wire_template->size() - n_matches);

          if (incomplete_template_ids.count(make_template_key(id)) == 0) {
            LOG4CPLUS_WARN(logger, "  Template match on wire template "
                                   "for domain "
                                       << observation_domain
                                       << " and template ID " << id
                                       << " successful, but incomplete");

            LOG4CPLUS_WARN(logger, "  List of unmatched IEs follows:");
            for (auto k = unmatched->begin(); k != unmatched->end(); ++k)
              LOG4CPLUS_WARN(logger, "    " << (*k)->toIESpec());
            incomplete_template_ids.insert(make_template_key(id));
          }
        }
        delete unmatched;

        matched_templates[wire_template] = *i;
        return *i;
      }
    }
    return 0;
  } else
    return m->second;
}

std::shared_ptr<ErrorContext>
PlacementContentHandler::start_data_set(uint16_t id, uint16_t length,
                                        const uint8_t *buf) {
  LOG4CPLUS_TRACE(logger, "ENTER start_data_set"
                              << ", id=" << id << ", length=" << length);

  // Find out who is interested in data from this data set
  const IETemplate *wire_template = find_wire_template(id);

  LOG4CPLUS_TRACE(logger, "  wire_template=" << wire_template);

  if (wire_template == 0) {
    if (unhandled_data_set_handler == 0) {
      if (unmatched_template_ids.count(make_template_key(id)) == 0) {
        LOG4CPLUS_WARN(logger, "  No placement for data set with "
                               "observation domain "
                                   << observation_domain << " and template id "
                                   << id
                                   << "; skipping"
                                      " (this warning will appear only once)");
        unmatched_template_ids.insert(make_template_key(id));
      }
      LIBFC_RETURN_OK();
    } else {
      std::shared_ptr<ErrorContext> e =
          unhandled_data_set_handler->unhandled_data_set(observation_domain, id,
                                                         length, buf);
      if (e->get_error() == Error::again) {
        wire_template = find_wire_template(id);
        if (wire_template == 0) {
          if (unmatched_template_ids.count(make_template_key(id)) == 0) {
            LOG4CPLUS_WARN(
                logger, "  No placement for data set with "
                        "observation domain "
                            << observation_domain << " and template id " << id
                            << "; skipping after second chance"
                               " (this warning will appear only once)");
            unmatched_template_ids.insert(make_template_key(id));
          }
          LIBFC_RETURN_OK();
        }
      }
    }
  }

  assert(wire_template != 0);

  const PlacementTemplate *placement_template =
      match_placement_template(id, wire_template);

  LOG4CPLUS_TRACE(logger, "  placement_template=" << placement_template);

  if (placement_template == 0) {
    LOG4CPLUS_TRACE(logger, "  no one interested in this data set; skipping");
    LIBFC_RETURN_OK();
  }

  DecodePlan plan(placement_template, wire_template);

  const uint8_t *buf_end = buf + length;
  const uint8_t *cur = buf;
  const uint16_t min_length = wire_template_min_length(wire_template);

  auto callback = callbacks.find(placement_template);
  assert(callback != callbacks.end());

  while (cur < buf_end && length >= min_length) {
    CH_REPORT_CALLBACK_ERROR(
        callback->second->start_placement(placement_template));
    uint16_t consumed = plan.execute(cur, length);
    CH_REPORT_CALLBACK_ERROR(
        callback->second->end_placement(placement_template));
    cur += consumed;
    length -= consumed;
  }

  LIBFC_RETURN_OK();
}

std::shared_ptr<ErrorContext> PlacementContentHandler::end_data_set() {
  LOG4CPLUS_TRACE(logger, "ENTER end_data_set");
  LOG4CPLUS_TRACE(logger, "LEAVE end_data_set");
  LIBFC_RETURN_OK();
}

void PlacementContentHandler::register_start_message_handler(
    PlacementCollector *callback) {
  start_message_handler = callback;
}

void PlacementContentHandler::register_placement_template(
    const PlacementTemplate *placement_template, PlacementCollector *callback) {
  placement_templates.push_back(placement_template);
  callbacks[placement_template] = callback;
}

void PlacementContentHandler::register_unhandled_data_set_handler(
    PlacementCollector *callback) {
  unhandled_data_set_handler = callback;
}

uint16_t
PlacementContentHandler::wire_template_min_length(const IETemplate *t) {
  uint16_t min = 0;

  for (auto i = t->begin(); i != t->end(); ++i) {
    if ((*i)->len() != kIpfixVarlen)
      min += (*i)->len();
  }
  return min;
}

} // namespace libfc
