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

#include "Imp.h"

namespace fcold {
    
    Imp::Imp(Backend *bep):
        worker(&Imp::work, this),
        backend(bep),
        worker_ectx(nullptr),
        run(true) {}
    
    void Imp::work() {
        while (run) {            
            std::shared_ptr<MessageBuffer> mb = next_mbuf();
            if (mb == nullptr) break;
            
            worker_ectx = collect(*mb);
            // FIXME do the right thing on error
        }
    }
    
    std::shared_ptr<MessageBuffer> Imp::next_mbuf() {
        std::unique_lock<std::mutex> lock(mbqmtx);
        
        while (mbq.size() == 0 && run) {
            mbqcv.wait(mbqmtx);
        }
        
        if (!run) return std::shared_ptr<MessageBuffer>(nullptr);
        return mbq.pop();
    }
    
    void Imp::enqueue_mbuf(std::shared_ptr<MessageBuffer> mb) {
        std::unique_lock<std::mutex> lock(mbqmtx);
        mbq.push(mb);
        mbqcv.notify_all();
    }
    
    void Imp::stop() {
        run = false;
        mbqcv.notify_all();
        worker.join();
    }

}