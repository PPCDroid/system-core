/* libs/pixelflinger/codeflinger/CodeCache.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "CodeCache"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include "codeflinger-mips/CodeCache.h"

namespace android {

// ----------------------------------------------------------------------------

#include <unistd.h>
#include <errno.h>

// ----------------------------------------------------------------------------

Assembly::Assembly(size_t size)
    : mCount(1), mSize(0)
{
    mBase = mMemBase = (uint32_t*)malloc(size);
    if (mBase) {
        mSize = size;
    }
}

Assembly::~Assembly()
{
    free(mMemBase);
}

void Assembly::incStrong(const void*) const
{
    android_atomic_inc(&mCount);
}

void Assembly::decStrong(const void*) const
{
    if (android_atomic_dec(&mCount) == 1) {
        delete this;
    }
}

ssize_t Assembly::size() const
{
    if (!mMemBase) return NO_MEMORY;
    return mSize;
}

uint32_t* Assembly::base() const
{
    return mBase;
}

void Assembly::setBase(uint32_t* newbase)
{
    mBase = newbase;
}

uint32_t* Assembly::membase() const
{
    return mMemBase;
}

ssize_t Assembly::resize(size_t newSize)
{
    uint32_t basediff;

    basediff = mBase - mMemBase;
    mMemBase = (uint32_t*)realloc(mMemBase, newSize);
    mBase = mMemBase + basediff;
    mSize = newSize;
    return size();
}

// ----------------------------------------------------------------------------

CodeCache::CodeCache(size_t size)
    : mCacheSize(size), mCacheInUse(0)
{
    pthread_mutex_init(&mLock, 0);
}

CodeCache::~CodeCache()
{
    pthread_mutex_destroy(&mLock);
}

sp<Assembly> CodeCache::lookup(const AssemblyKeyBase& keyBase) const
{
    pthread_mutex_lock(&mLock);
    sp<Assembly> r;
    ssize_t index = mCacheData.indexOfKey(key_t(keyBase));
    if (index >= 0) {
        const cache_entry_t& e = mCacheData.valueAt(index);
        e.when = mWhen++;
        r = e.entry;
    }
    pthread_mutex_unlock(&mLock);
    return r;
}

int CodeCache::cache(  const AssemblyKeyBase& keyBase,
                            const sp<Assembly>& assembly)
{
    pthread_mutex_lock(&mLock);

    const ssize_t assemblySize = assembly->size();
    while (mCacheInUse + assemblySize > mCacheSize) {
        // evict the LRU
        size_t lru = 0;
        size_t count = mCacheData.size();
        for (size_t i=0 ; i<count ; i++) {
            const cache_entry_t& e = mCacheData.valueAt(i);
            if (e.when < mCacheData.valueAt(lru).when) {
                lru = i;
            }
        }
        const cache_entry_t& e = mCacheData.valueAt(lru);
	LOGI("evict %ld", e.entry->size());
        mCacheInUse -= e.entry->size();
        mCacheData.removeItemsAt(lru);
    }

    ssize_t err = mCacheData.add(key_t(keyBase), cache_entry_t(assembly, mWhen));
    if (err >= 0) {
        mCacheInUse += assemblySize;
        mWhen++;
        // synchronize caches...
        const long base = long(assembly->base());
        const long bytes = long(assembly->size());
        err = cacheflush(base, bytes, 0);
        LOGE_IF(err, "__MIPS_NR_cacheflush error %s\n",
                strerror(errno));
    }

    pthread_mutex_unlock(&mLock);
    return err;
}

// ----------------------------------------------------------------------------

}; // namespace android
