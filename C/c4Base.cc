//
//  c4Base.cc
//  Couchbase Lite Core
//
//  Created by Jens Alfke on 8/1/16.
//  Copyright (c) 2016 Couchbase. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.

#include "c4Internal.hh"
#include "c4Database.h"
#include "c4Document.h"
#include "c4Private.h"

#include "Logging.hh"

#include "SQLiteCpp/Exception.h"
#include <ctype.h>
#include <algorithm>
#include <mutex>

using namespace litecore;


#pragma mark - ERRORS:


namespace c4Internal {

    void recordError(C4ErrorDomain domain, int code, C4Error* outError) noexcept {
        if (outError) {
            outError->domain = domain;
            outError->code = code;
        }
    }

    void recordException(const exception &e, C4Error* outError) noexcept {
        static const C4ErrorDomain domainMap[] = {LiteCoreDomain, POSIXDomain,
                                                  ForestDBDomain, SQLiteDomain};
        error err = error::convertException(e).standardized();
        recordError(domainMap[err.domain], err.code, outError);
    }


    bool tryCatch(C4Error *error, std::function<void()> fn) noexcept {
        try {
            fn();
            return true;
        } catchError(error);
        return false;
    }

}


C4SliceResult c4error_getMessage(C4Error err) noexcept {
    if (err.code == 0) {
        return sliceResult(nullptr);
    } else if (err.domain < 1 || err.domain > SQLiteDomain) {
        return sliceResult("unknown error domain");
    } else {
        static constexpr error::Domain kDomains[] = {error::LiteCore, error::POSIX,
                                                     error::ForestDB, error::SQLite};
        error e(kDomains[err.domain - 1], err.code);
        return sliceResult(e.what());
    }
}

char* c4error_getMessageC(C4Error error, char buffer[], size_t bufferSize) noexcept {
    C4SliceResult msg = c4error_getMessage(error);
    auto len = min(msg.size, bufferSize-1);
    if (msg.buf)
        memcpy(buffer, msg.buf, len);
    buffer[len] = '\0';
    c4slice_free(msg);
    return buffer;
}


int c4_getObjectCount() noexcept {
    return InstanceCounted::gObjectCount;
}


#pragma mark - SLICES:


bool c4SliceEqual(C4Slice a, C4Slice b) noexcept {
    return a == b;
}


void c4slice_free(C4SliceResult slice) noexcept {
    alloc_slice::release({slice.buf, slice.size});
}


namespace c4Internal {

    C4SliceResult sliceResult(alloc_slice s) {
        s.retain();
        return {s.buf, s.size};
    }

    C4SliceResult sliceResult(slice s) {
        return sliceResult(alloc_slice(s));
    }

    C4SliceResult sliceResult(const char *str) {
        if (str)
            return sliceResult(slice{str, strlen(str)});
        else
            return {nullptr, 0};
    }

}


#pragma mark - LOGGING:


static C4LogCallback clientLogCallback;


static void logCallback(const LogDomain &domain, LogLevel level, const char *message) {
    auto cb = clientLogCallback;
    if (cb)
        cb((C4LogLevel)level, slice(message));
}


void c4log_register(C4LogLevel level, C4LogCallback callback) noexcept {
    if (callback) {
        LogDomain::MinLevel = (LogLevel)level;
        LogDomain::Callback = logCallback;
    } else {
        LogDomain::MinLevel = LogLevel::None;
        LogDomain::Callback = nullptr;
    }
    clientLogCallback = callback;
}


void c4log_setLevel(const char *domainName, C4LogLevel level) noexcept {
    auto domain = LogDomain::named(domainName);
    if (domain)
        domain->setLevel((LogLevel)level);
    else
        Warn("c4log_setLevel: No log domain named \"%s\"", domainName);
}


void c4log_warnOnErrors(bool warn) noexcept {
    error::sWarnOnError = warn;
}


void c4log(C4LogLevel level, const char *fmt, ...) {
    if (_usuallyFalse(DefaultLog.willLog((LogLevel)level))) {
        va_list args;
        va_start(args, fmt);
        try {
            DefaultLog.vlog((LogLevel)level, fmt, args);
        } catch (...) { }
        va_end(args);
    }
}
