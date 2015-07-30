#ifndef _TGRUTLOOP_H_
#define _TGRUTLOOP_H_

#include "TNamed.h"

#include "TDataLoop.h"

// So that rootcint doesn't see this
class RawDataQueue;
class TRootOutfile;
class TNSCLEvent;
class TGEBEvent;

class TGRUTLoop : public TDataLoop {
public:
  static TGRUTLoop* Get();

  virtual ~TGRUTLoop();
  virtual int ProcessEvent(TRawEvent& event);

  using TDataLoop::ProcessFile;
  void ProcessFile(const char* input, const char* output);

  void PrintQueue();
  void StatusQueue();

  TRawEvent GetEvent();

  virtual void Initialize();
  virtual void Finalize();

  void PrintOutfile();

  bool FillCondition(TRawEvent& event);

  void Status();

private:
  template<typename T, typename... Params>
  friend void TDataLoop::CreateDataLoop(Params&&... params);

  void ProcessFromQueue(TRawEvent& event);

  TGRUTLoop();
  void WriteLoop();

  void HandleBuiltNSCLData(TNSCLEvent& event);
  void HandleUnbuiltNSCLData(TNSCLEvent& event);
  void HandleGEBData(TGEBEvent& event);

  RawDataQueue* queue;
  TRootOutfile* outfile;

#ifndef __CINT__
  std::thread write_thread;
#endif


  ClassDef(TGRUTLoop,0);
};

#endif /* _TGRUTLOOP_H_ */