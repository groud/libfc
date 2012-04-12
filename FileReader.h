/**
 * @file
 * @author Brian Trammell <trammell@tik.ee.ethz.ch>
 *
 * @section DESCRIPTION
 * 
 * A FileReader is a collector that reads from a file.
 *
 */

#ifndef IPFIX_FILEREADER_H // idem
#define IPFIX_FILEREADER_H // hack

#include <cstdio>
#include "Collector.h"

namespace IPFIX {
 
class FileReader : public Collector {
public:
  
/**
 * Create a new FileReader for a given filename. 
 *
 * @param filename name of file to read. Use "-" for stdin.
 */
  FileReader(std::string filename):
    Collector(),
    session_(new Session()),
    filename_(filename),
    fp_(NULL) {
    }

  /**
   * FileReader destructor; ensures the file is closed.
   */
  virtual ~FileReader();

protected:
  void ensureFP();

  virtual bool _receiveMessage(MBuf& mbuf, std::tr1::shared_ptr<Session>& session);
  
private:
  std::tr1::shared_ptr<Session>     session_;
  std::string                       filename_;
  mutable FILE                      *fp_;
};

} // namespace IPFIX

#endif // idem hack