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

#include "IETemplate.h"
#include "InfoElement.h"

/**
 * @file
 * @author Brian Trammell <trammell@tik.ee.ethz.ch>
 *
 * @section DESCRIPTION
 *
 * This file specifies the interface to MatchTemplate. A MatchTemplate
 * is an IETemplate which can only be used for minimal template matching
 * (i.e., it stores no offset information used for encoding or decoding,
 * and is not bound to a session or ID).
 */

#ifndef IPFIX_MATCHTEMPLATE_H // idem
#define IPFIX_MATCHTEMPLATE_H // hack

namespace IPFIX {

class MatchTemplate : public IETemplate {

public:
    
    MatchTemplate():
    IETemplate() {}

    void activate() {}
    
    void dumpIdent(std::ostream &os) const {
        os << "*** MatchTemplate " << reinterpret_cast<uint64_t> (this) << std::endl;
    }
    
    void add(const InfoElement* ie) {
        add_inner(ie);

        if (ie->len() == kVarlen) {
            minlen_ += 1;
        } else {
            minlen_ += ie->len();
        }
        
        // we don't care about offsets but
        // lots of template code assumes they're there...
        offsets_.push_back(0);
    }
    
    void clear() {
        ies_.clear();
        index_map_.clear();
        offsets_.clear();
        minlen_ = 0;
    }
  
    void mimic(const IETemplate& rhs) {
        clear();
        for (IETemplateIter i = rhs.begin(); i != rhs.end(); ++i) {
            add(*i);
        }
    }

};

}

#endif
