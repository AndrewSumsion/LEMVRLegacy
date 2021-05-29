#include "LemvrMain.h"

#include "FrameBuffer.h"
#include "emugl/common/thread.h"

#include <iostream>
#include <stdint.h>

namespace lemvr {

class LemvrThread : public emugl::Thread {
public:
    LemvrThread() : Thread() {}

    virtual intptr_t main() {
        FrameBuffer::waitUntilInitialized();
        std::cout << "lemvr: Frame Buffer Initialized\n";
        return 0;
    }
};

void lemvrMain() {
    LemvrThread* thread = new LemvrThread();

    thread->start();

    int exitStatus;
    thread->wait((intptr_t*)&exitStatus);
}

}