#ifdef HN_PLATFORM_MACOS
#import <Cocoa/Cocoa.h>
#include <vector>
#include <string>
#include <cstdlib>

static NSArray<NSString*>* HN_ParseCSVExtensions(const char* csv) {
    if (!csv || !*csv) return nil;
    NSMutableArray<NSString*>* arr = [NSMutableArray array];
    const char* p = csv;
    const char* start = p;
    while (*p) {
        if (*p == ',') {
            if (p > start) {
                NSString* s = [[NSString alloc] initWithBytes:start length:(p - start) encoding:NSUTF8StringEncoding];
                if (s.length > 0) [arr addObject:[s lowercaseString]];
                [s release];
            }
            start = p + 1;
        }
        ++p;
    }
    if (p > start) {
        NSString* s = [[NSString alloc] initWithBytes:start length:(p - start) encoding:NSUTF8StringEncoding];
        if (s.length > 0) [arr addObject:[s lowercaseString]];
        [s release];
    }
    return arr.count > 0 ? arr : nil;
}

extern "C" const char* hn_mac_open_file(const char* extensions_csv) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        panel.allowedFileTypes = HN_ParseCSVExtensions(extensions_csv);

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URL;
            if (url) {
                const char* utf8 = url.path.UTF8String;
                if (utf8) {
                    // Return a heap copy the C++ side can free()
                    return strdup(utf8);
                }
            }
        }
        return nullptr;
    }
}

extern "C" const char* hn_mac_save_file(const char* extensions_csv) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;
        panel.allowedFileTypes = HN_ParseCSVExtensions(extensions_csv);

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URL;
            if (url) {
                const char* utf8 = url.path.UTF8String;
                if (utf8) {
                    return strdup(utf8);
                }
            }
        }
        return nullptr;
    }
}
#endif