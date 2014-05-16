//
//  CBForestPrivate.m
//  CBForest
//
//  Created by Jens Alfke on 9/5/12.
//  Copyright (c) 2012 Couchbase, Inc. All rights reserved.
//

#import "CBForestPrivate.h"
#import "rev_tree.h"


BOOL CheckFailed(fdb_status code, id key, NSError** outError) {
    static NSString* const kErrorNames[] = {
        nil,
        @"Invalid arguments",
        @"Open failed",
        @"File not found",
        @"Write failed",
        @"Read failed",
        @"Close failed",
        @"Commit failed",
        @"Memory allocation failed",
        @"Key not found",
        @"Database is read-only",
        @"Compaction failed",
        @"Iterator failed",
        @"Seek failed",
        @"Fsync failed",
        @"Invalid data checksum",
        @"Database corrupted",
        @"Data compression failed",
        @"No database instance",
        @"Rollback in progress",
        @"Invalid database config",
        @"Manual compaction unavailable",
    };
    NSString* errorName;
    if (code < 0 && -code < (sizeof(kErrorNames)/sizeof(id)))
        errorName = [NSString stringWithFormat: @"ForestDB error: %@", kErrorNames[-code]];
    else if ((int)code == kCBForestErrorRevisionDataCorrupt)
        errorName = @"Revision data is corrupted";
    else
        errorName = [NSString stringWithFormat: @"ForestDB error %d", code];
    if (outError) {
        NSMutableDictionary* info = [NSMutableDictionary dictionaryWithObject: errorName
                                                            forKey: NSLocalizedDescriptionKey];
        if (key)
            info[@"Key"] = key;
        *outError = [NSError errorWithDomain: CBForestErrorDomain code: code userInfo: info];
    } else {
        NSLog(@"Warning: CBForest error %d (%@)", code, errorName);
    }
    return NO;
}


BOOL CBIsFileNotFoundError( NSError* error ) {
    NSString* domain = error.domain;
    NSInteger code = error.code;
    return ([domain isEqualToString: NSPOSIXErrorDomain] && code == ENOENT)
        || ([domain isEqualToString: NSCocoaErrorDomain] && code == NSFileNoSuchFileError);
}


NSData* JSONToData(UU id obj, NSError** outError) {
    if ([obj isKindOfClass: [NSDictionary class]] || [obj isKindOfClass: [NSArray class]]) {
        return [NSJSONSerialization dataWithJSONObject: obj options: 0 error: outError];
    } else {
        NSArray* array = [[NSArray alloc] initWithObjects: &obj count: 1];
        NSData* data = [NSJSONSerialization dataWithJSONObject: array options: 0 error: outError];
        return [data subdataWithRange: NSMakeRange(1, data.length - 2)];
    }
}


id SliceToJSON(slice buf, NSError** outError) {
    if (buf.size == 0)
        return nil;
    NSCAssert(buf.buf != NULL, @"SliceToJSON(NULL)");
    NSData* data = [[NSData alloc] initWithBytesNoCopy: (void*)buf.buf
                                                length: buf.size
                                          freeWhenDone: NO];
    return DataToJSON(data, outError);
}


void UpdateBuffer(void** outBuf, size_t *outLen, const void* srcBuf, size_t srcLen) {
    free(*outBuf);
    *outBuf = NULL;
    *outLen = srcLen;
    if (srcLen > 0) {
        *outBuf = malloc(srcLen);
        memcpy(*outBuf, srcBuf, srcLen);
    }

}

void UpdateBufferFromData(void** outBuf, size_t *outLen, NSData* data) {
    UpdateBuffer(outBuf, outLen, data.bytes, data.length);
}




// Calls the given block, passing it a UTF-8 encoded version of the given string.
// The UTF-8 data can be safely modified by the block but isn't valid after the block exits.
BOOL WithMutableUTF8(UU NSString* str, void (^block)(uint8_t*, size_t)) {
    NSUInteger byteCount;
    if (str.length < 256) {
        // First try to copy the UTF-8 into a smallish stack-based buffer:
        uint8_t stackBuf[256];
        NSRange remaining;
        BOOL ok = [str getBytes: stackBuf maxLength: sizeof(stackBuf) usedLength: &byteCount
                       encoding: NSUTF8StringEncoding options: 0
                          range: NSMakeRange(0, str.length) remainingRange: &remaining];
        if (ok && remaining.length == 0) {
            block(stackBuf, byteCount);
            return YES;
        }
    }

    // Otherwise malloc a buffer to copy the UTF-8 into:
    NSUInteger maxByteCount = [str maximumLengthOfBytesUsingEncoding: NSUTF8StringEncoding];
    uint8_t* buf = malloc(maxByteCount);
    if (!buf)
        return NO;
    BOOL ok = [str getBytes: buf maxLength: maxByteCount usedLength: &byteCount
                   encoding: NSUTF8StringEncoding options: 0
                      range: NSMakeRange(0, str.length) remainingRange: NULL];
    if (ok)
        block(buf, byteCount);
    free(buf);
    return ok;
}


#pragma mark - REVISION IDS:


NSData* CompactRevID(UU NSString* revID) {
    if (!revID)
        return nil;
    __block NSMutableData* data;
    WithMutableUTF8(revID, ^(uint8_t *chars, size_t length) {
        data = [[NSMutableData alloc] initWithLength: length];
        __block slice dst = DataToSlice(data);
        if (RevIDCompact((slice){chars,length}, &dst))
            data.length = dst.size;
        else
            data = nil;
    });
    return data;
}


NSString* ExpandRevID(slice compressedRevID) {
    //OPT: This is not very efficient.
    size_t size = RevIDExpandedSize(compressedRevID);
    if (size == 0)
        return SliceToString(compressedRevID.buf, compressedRevID.size);
    NSMutableData* data = [[NSMutableData alloc] initWithLength: size];
    slice buf = DataToSlice(data);
    RevIDExpand(compressedRevID, &buf);
    data.length = buf.size;
    return [[NSString alloc] initWithData: data encoding: NSASCIIStringEncoding];
}


NSUInteger CPUCount(void) {
    static NSUInteger sCount;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sCount = [[NSProcessInfo processInfo] processorCount];
    });
    return sCount;
}


@implementation CBForestToken
{
    id _owner;
    NSCondition* _condition;
}

@synthesize name=_name;

- (instancetype)init {
    self = [super init];
    if (self) {
        _condition = [[NSCondition alloc] init];
    }
    return self;
}

- (void) lockWithOwner: (UU id)owner {
    NSParameterAssert(owner != nil);
    [_condition lock];
    while (_owner != nil && _owner != owner)
        [_condition wait];
    _owner = owner;
    [_condition unlock];
}

- (void) unlockWithOwner: (UU id)oldOwner {
    NSParameterAssert(oldOwner != nil);
    [_condition lock];
    NSAssert(oldOwner == _owner, @"clearing wrong owner! (%p, expected %p)", oldOwner, _owner);
    _owner = nil;
    [_condition broadcast];
    [_condition unlock];
}
@end
