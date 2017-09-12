// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/Optional.h"
#include "android/base/system/System.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/Kext/KextManager.h>
#include <IOKit/storage/IOBlockStorageDevice.h>

#include <stdlib.h>
#include <sys/stat.h>

namespace android {
namespace base {

Optional<System::DiskKind> nativeDiskKind(int st_dev) {
    const char* devName = devname(st_dev, S_IFBLK);
    if (!devName) {
        return {};
    }
    CFMutableDictionaryRef classesToMatch =
            IOBSDNameMatching(kIOMasterPortDefault, 0, devName);
    if (!classesToMatch) {
        NSLog(@"Could not find io classes of disk");
        return {};
    }

    // get iterator of matching services
    io_iterator_t entryIterator;

    if (KERN_SUCCESS != IOServiceGetMatchingServices(kIOMasterPortDefault,
                                                     classesToMatch,
                                                     &entryIterator)) {
        NSLog(@"Can't iterate services");
        return {};
    }

    // iterate over all found medias
    io_object_t serviceEntry, parentMedia;
    while ((serviceEntry = IOIteratorNext(entryIterator)) != 0) {
        // We assume there won't be more levels of nesting here. The limit is
        // arbitrary and can be increased if we hit it.
        int maxlevels = 8;
        do {
            kern_return_t kernResult = IORegistryEntryGetParentEntry(
                    serviceEntry, kIOServicePlane, &parentMedia);
            IOObjectRelease(serviceEntry);

            if (KERN_SUCCESS != kernResult) {
                serviceEntry = 0;
                NSLog(@"Error while getting parent service entry");
                break;
            }

            serviceEntry = parentMedia;
            if (!parentMedia) {
                break;  // finished iterator
            }

            CFTypeRef res = IORegistryEntryCreateCFProperty(
                    serviceEntry, CFSTR(kIOPropertyDeviceCharacteristicsKey),
                    kCFAllocatorDefault, 0);
            if (res) {
                NSString* type = [(NSDictionary*)res
                        objectForKey:(id)CFSTR(kIOPropertyMediumTypeKey)];
                if ([@kIOPropertyMediumTypeSolidStateKey
                            isEqualToString:type]) {
                    CFRelease(res);
                    return System::DiskKind::Ssd;
                } else if ([@kIOPropertyMediumTypeRotationalKey
                                   isEqualToString:type]) {
                    CFRelease(res);
                    return System::DiskKind::Hdd;
                }
                CFRelease(res);
            }
        } while (maxlevels--);

        if (serviceEntry) {
            IOObjectRelease(serviceEntry);
        }
    }
    IOObjectRelease(entryIterator);

    return {};
}

}  // namespace base
}  // namespace android
