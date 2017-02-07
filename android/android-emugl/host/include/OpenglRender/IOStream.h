/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#pragma once

#include "ErrorLog.h"
#include "android/base/files/Stream.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

class IOStream {
protected:
    explicit IOStream(size_t bufSize) : m_bufsize(bufSize) {}

    ~IOStream() {
        // NOTE: m_buf was owned by the child class thus we expect it to be
        // released before the object destruction.
    }

public:
    size_t read(void* buf, size_t bufLen) {
        if (!readRaw(buf, &bufLen)) {
            return 0;
        }
        return bufLen;
    }

    unsigned char* alloc(size_t len) {
        if (m_buf && len > m_free) {
            if (flush() < 0) {
                ERR("Failed to flush in alloc\n");
                return NULL; // we failed to flush so something is wrong
            }
        }

        if (!m_buf || len > m_bufsize) {
            int allocLen = m_bufsize < len ? len : m_bufsize;
            m_buf = (unsigned char *)allocBuffer(allocLen);
            if (!m_buf) {
                ERR("Alloc (%u bytes) failed\n", allocLen);
                return NULL;
            }
            m_bufsize = m_free = allocLen;
        }

        unsigned char* ptr = m_buf + (m_bufsize - m_free);
        m_free -= len;

        return ptr;
    }

    int flush() {
        if (!m_buf || m_free == m_bufsize) return 0;

        int stat = commitBuffer(m_bufsize - m_free);
        m_buf = NULL;
        m_free = 0;
        return stat;
    }

    void save(android::base::Stream* stream) {
        stream->putBe32(m_bufsize);
        stream->putBe32(m_free);
        onSave(stream);
    }

    void load(android::base::Stream* stream) {
        m_bufsize = stream->getBe32();
        m_free = stream->getBe32();
        m_buf = onLoad(stream);
    }

    virtual void* getDmaForReading(uint64_t guest_paddr) = 0;
    virtual void unlockDma(uint64_t guest_paddr) = 0;

protected:
    virtual void *allocBuffer(size_t minSize) = 0;
    virtual int commitBuffer(size_t size) = 0;
    virtual const unsigned char *readRaw(void *buf, size_t *inout_len) = 0;
    virtual void onSave(android::base::Stream* stream) = 0;
    virtual unsigned char* onLoad(android::base::Stream* stream) = 0;

private:
    unsigned char* m_buf = nullptr;
    size_t m_bufsize;
    size_t m_free = 0;
};
