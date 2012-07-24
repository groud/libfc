/* Copyright (c) 2011-2012 ETH Zürich. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the names of NEC Europe Ltd, Consorzio Nazionale 
 *      Interuniversitario per le Telecomunicazioni, Institut Telecom/Telecom 
 *      Bretagne, ETH Zürich, INVEA-TECH a.s. nor the names of its contributors 
 *      may be used to endorse or promote products derived from this software 
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT 
 * HOLDERBE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER 
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 */

#define BOOST_TEST_DYN_LINK
#include <boost/test/parameterized_test.hpp>
#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test.hpp>

#include "TestCommon.h"

#include <cerrno>
#include <pcap.h>

const uint16_t kEthertypeIP4 = 0x0800;

struct ethhdr {
  uint8_t     smac[6];
  uint8_t     dmac[6];
  uint16_t    ethertype;
};

using namespace IPFIX;

static StructTemplate caftmpl;
static int datalink;

class CapfixReceiver : public SetReceiver {
private:
  pcap_dumper_t* dumper_;
  
public: 
  CapfixReceiver(pcap_dumper_t* dumper): dumper_(dumper) {}
  
  void receiveSet(const Collector* collector, 
                       Transcoder& setxc, 
                       const WireTemplate* wt)  {
    
    static int set_count = 0;
    static int rec_count = 0;
    
    CapfixPacket pkt;
    pcap_pkthdr caphdr;
    
    std::cerr << "template id " << wt->tid() << " minlen " << wt->minlen();
    
    while (wt->decodeStruct(setxc, caftmpl, reinterpret_cast<uint8_t*>(&pkt))) {
      caphdr.ts.tv_sec = pkt.observationTimeMilliseconds / 1000;
      caphdr.ts.tv_usec = (pkt.observationTimeMilliseconds % 1000) * 1000;
      caphdr.len = pkt.ipTotalLength;
      caphdr.caplen = pkt.ipHeaderPacketSection.len;
      
      std::cerr << " in set " << std::dec << set_count << " record " << rec_count << 
                   " at " << std::hex << reinterpret_cast<uint64_t>(setxc.cur()) << std::endl;
      
      std::cerr << "capfix receiver got packet ts " << std::dec << caphdr.ts.tv_sec <<
                   " length " << caphdr.len << " caplen " << caphdr.caplen <<
                   " pktbuf " << std::hex << reinterpret_cast<uint64_t>(pkt.ipHeaderPacketSection.cp) << 
                   " pktbuf[0] " << static_cast<int>(pkt.ipHeaderPacketSection.cp[0]) << std::endl;
      
      pcap_dump(reinterpret_cast<unsigned char*>(dumper_), &caphdr, pkt.ipHeaderPacketSection.cp);
      rec_count++;
  }
    
    set_count++;
    
  }
};


void exportPacket (unsigned char *evp,
                   const pcap_pkthdr* caphdr,
                   const uint8_t* capbuf) {
  FileWriter* e = reinterpret_cast<FileWriter *>(evp);
  CapfixPacket pkt;
  size_t caplen = caphdr->caplen;
  
  if (datalink == DLT_EN10MB) {
    const ethhdr* eh = reinterpret_cast<const ethhdr*>(capbuf);
    if (ntohs(eh->ethertype) == kEthertypeIP4) {
      caplen -= sizeof(ethhdr);
      capbuf += sizeof(ethhdr);
    } else {
      return;
    }
  }
  
  pkt.observationTimeMilliseconds = (uint64_t)caphdr->ts.tv_sec * 1000;
  pkt.observationTimeMilliseconds += caphdr->ts.tv_usec / 1000;
  pkt.ipTotalLength = caphdr->len;
  pkt.ipHeaderPacketSection.len = (size_t)caphdr->caplen;
  pkt.ipHeaderPacketSection.cp = (uint8_t *)capbuf;
  
  e->setTemplate(kCapfixPacketTid);
  e->exportStruct(caftmpl, reinterpret_cast<uint8_t*>(&pkt));
}

std::string new_extension(const std::string& filename, const std::string& extension) {
  size_t dpos = filename.rfind('.');
  if (dpos == std::string::npos) {
    return filename + std::string(".") + extension;
  } else {
    return filename.substr(0, dpos + 1) + extension;
  }
}

int main_to_pcap(const std::string& filename) {

  // open an ipfix source
  FileReader fr(filename);
  pcap_t* pcap = pcap_open_dead(DLT_RAW, 65535);
  pcap_dumper_t* dumper = pcap_dump_open(pcap, new_extension(filename, std::string("pcap")).c_str());
  if (!dumper) {
    std::cerr << "failed to open pcap dumper: " << pcap_geterr(pcap) << std::endl;
    return 1;
  }
  
  // create a capfix receiver around the output file
  CapfixReceiver cr(dumper);

  // register our set receiver
  fr.registerReceiver(&caftmpl, &cr);
  
  while (!didQuit()) {
    MBuf mbuf;
    if (!fr.receiveMessage(mbuf)) { doQuit(0); }
  }
  
  // clean up (file reader closes automatically on destruction)
  pcap_dump_close(dumper);
  return 0;
}

int main_to_ipfix(const std::string& filename) {
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t *pcap;
  
  // open a pcap source
  pcap = pcap_open_offline(filename.c_str(), errbuf);
  if (!pcap) {
    std::cerr << "failed to open pcap file: " << errbuf << std::endl;
    return 1;
  }
  
  // make sure we like the datalink
  datalink = pcap_datalink(pcap);
  switch (datalink) {
    case DLT_EN10MB:
    case DLT_RAW:
      break;
    default:
      std::cerr << "capfix only groks Ethernet and raw IP frames." << std::endl;
      return 1;
      break;
  }
  
  // set up output
  FileWriter fw(new_extension(filename, std::string("ipfix")), kTestDomain);
  fw.getTemplate(kCapfixPacketTid)->mimic(caftmpl);
  fw.getTemplate(kCapfixPacketTid)->dump(std::cerr);
  fw.exportTemplatesForDomain();
  
  // runloop
  int rv;
  while (!didQuit()) {
    int pcrv = pcap_dispatch(pcap, 1, exportPacket, reinterpret_cast<uint8_t *>(&fw));
    if (pcrv == 0) {
      // no packets, normal exit
      rv = 0;
      break;
    } else if (pcrv < 0) {
      // something broke, run away screaming
      std::cerr << "pcap error: " << errbuf << std::endl;
      rv = 1;
      break;
    }
  }
  
  // clean up
  fw.flush();
  pcap_close(pcap);
  return rv;
}

static int test_capfix (const std::string& filename) {
  
  // set up signal handlers
  install_quit_handler();
  
  // create an information model for IPFIX, no biflows
  InfoModel::instance().defaultIPFIX();
  
  // initialize global structure template
  makeCapfixPacketTemplate(caftmpl);
  caftmpl.dump(std::cerr);
  
  // determine what kind of file we're looking at
  if (filename.find(".ipfix", filename.length() - 6) != std::string::npos) {
    return main_to_pcap(filename);
  } else if (filename.find(".pcap", filename.length() - 5) != std::string::npos) {
    return main_to_ipfix(filename);
  } 
  
  std::cerr << "need a filename ending in .ipfix or .pcap to continue..." << std::endl;
  return 1;
}

#if 0
BOOST_AUTO_TEST_SUITE(Capfix)

BOOST_AUTO_TEST_CASE(CapfixTest) {
  const char* filenames[] = {"test01.ipfix", "test01.pcap"};

  std::for_each(filenames, filenames + sizeof(filenames)/sizeof(filenames[0]),
                [] (const char* filename) {
                  BOOST_CHECK_EQUAL(test_capfix(filename), 0);
                });
}

BOOST_AUTO_TEST_SUITE_END()
#endif
