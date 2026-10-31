// Manosaba: two-way save sync core (see ICloudSaveSyncCore.h).
#import "ICloudSaveSyncCore.h"
#import <CommonCrypto/CommonDigest.h>

static NSString* ManosabaHash(NSData* data)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString* hex = [NSMutableString stringWithCapacity:CC_SHA256_DIGEST_LENGTH * 2];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) [hex appendFormat:@"%02x", digest[i]];
    return hex;
}

static NSDate* ManosabaModDate(NSURL* url)
{
    NSDate* date = nil;
    [url getResourceValue:&date forKey:NSURLContentModificationDateKey error:nil];
    return date ?: [NSDate distantPast];
}

// What is synced: every save (*.nson: slots, quick/auto saves, GlobalSave) and the game's settings.
extern "C" BOOL ManosabaIsSyncedName(NSString* name)
{
    // A plain file name only: names also come back from the manifest, and a path there must
    // never let the sync read, overwrite or move anything outside the two save folders.
    if (name.length == 0 || [name hasPrefix:@"."] || [name containsString:@"/"]) return NO;
    return [name.pathExtension isEqualToString:@"nson"] || [name isEqualToString:@"Settings.json"];
}

// Synced files in a folder. iCloud placeholders (".Name.icloud") count as present but unreadable.
static NSSet<NSString*>* ManosabaSaveNames(NSURL* dir, NSMutableSet<NSString*>* placeholders)
{
    NSMutableSet* names = [NSMutableSet set];
    for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir.path error:nil])
    {
        if ([f hasPrefix:@"."] && [f hasSuffix:@".icloud"])
        {
            NSString* name = [[f substringFromIndex:1] stringByDeletingPathExtension];
            if (!ManosabaIsSyncedName(name)) continue;
            [names addObject:name];
            [placeholders addObject:name];
        }
        else if (ManosabaIsSyncedName(f))
            [names addObject:f];
    }
    return names;
}

static NSData* ManosabaReadCloud(NSURL* url, NSError** error)
{
    __block NSData* data = nil;
    __block NSError* readError = nil;
    [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateReadingItemAtURL:url options:0 error:error byAccessor:^(NSURL* u) {
        data = [NSData dataWithContentsOfURL:u options:0 error:&readError];
    }];
    if (!data && error && !*error) *error = readError;
    return data;
}

static BOOL ManosabaWrite(NSData* data, NSDate* date, NSURL* url, BOOL coordinated, NSError** error)
{
    __block BOOL ok = NO;
    __block NSError* writeError = nil;
    void (^write)(NSURL*) = ^(NSURL* u) {
        ok = [data writeToURL:u options:NSDataWritingAtomic error:&writeError];
        if (ok) [[NSFileManager defaultManager] setAttributes:@{NSFileModificationDate: date} ofItemAtPath:u.path error:nil];
    };
    if (coordinated)
        [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateWritingItemAtURL:url options:NSFileCoordinatorWritingForReplacing error:error byAccessor:write];
    else
        write(url);
    if (!ok && error && !*error) *error = writeError;
    return ok;
}

static BOOL ManosabaRemove(NSURL* url, BOOL coordinated, NSError** error)
{
    __block BOOL ok = NO;
    __block NSError* removeError = nil;
    void (^remove)(NSURL*) = ^(NSURL* u) { ok = [[NSFileManager defaultManager] removeItemAtURL:u error:&removeError]; };
    if (coordinated)
        [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateWritingItemAtURL:url options:NSFileCoordinatorWritingForDeleting error:error byAccessor:remove];
    else
        remove(url);
    if (!ok && error && !*error) *error = removeError;
    return ok;
}

NSDictionary<NSString*, NSNumber*>* ManosabaSyncSaves(NSURL* local, NSURL* cloud, NSURL* manifestURL,
                                                     ManosabaSyncMode mode, NSTimeInterval settle,
                                                     BOOL (^cloudReady)(NSURL* url),
                                                     BOOL (^allowLocalWrites)(void))
{
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm createDirectoryAtURL:local withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtURL:cloud withIntermediateDirectories:YES attributes:nil error:nil];

    NSMutableDictionary<NSString*, NSString*>* manifest = [NSMutableDictionary dictionary];
    NSData* manifestData = [NSData dataWithContentsOfURL:manifestURL];
    // A device that has never synced joins the shared set: on a mismatch iCloud wins (the local
    // copy goes to the trash folder), so stale or test saves never overwrite real progress.
    BOOL firstSync = manifestData == nil;
    if (manifestData)
    {
        id parsed = [NSJSONSerialization JSONObjectWithData:manifestData options:0 error:nil];
        if ([parsed isKindOfClass:[NSDictionary class]]) [manifest addEntriesFromDictionary:parsed];
    }

    NSMutableDictionary<NSString*, NSNumber*>* stats = [@{@"pulled": @0, @"pushed": @0, @"deletedLocal": @0, @"deletedCloud": @0,
                                                          @"conflicts": @0, @"skipped": @0, @"errors": @0} mutableCopy];
    void (^count)(NSString*) = ^(NSString* key) { stats[key] = @(stats[key].integerValue + 1); };

    NSMutableSet<NSString*>* placeholders = [NSMutableSet set];
    NSSet<NSString*>* cloudNames = ManosabaSaveNames(cloud, placeholders);
    if (cloudNames.count == 0 && manifest.count > 0)
    {
        // An empty iCloud folder after a sync means it was reset (or another Apple ID), not that
        // every save was deleted elsewhere: forget the history and upload the local saves again.
        printf("[icloud] iCloud folder is empty, re-uploading local saves\n");
        [manifest removeAllObjects];
    }
    NSMutableSet<NSString*>* names = [NSMutableSet setWithSet:ManosabaSaveNames(local, nil)];
    [names unionSet:cloudNames];
    for (NSString* name in manifest.allKeys)
        if (ManosabaIsSyncedName(name)) [names addObject:name];
    // Saves deleted on the other device are moved here instead of being removed outright.
    NSURL* trash = [manifestURL.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"manosaba-icloud-trash" isDirectory:YES];
    BOOL (^toTrash)(NSURL*, NSString*, NSError**) = ^BOOL(NSURL* file, NSString* name, NSError** err) {
        [fm createDirectoryAtURL:trash withIntermediateDirectories:YES attributes:nil error:nil];
        NSURL* dest = [trash URLByAppendingPathComponent:[NSString stringWithFormat:@"%.0f-%@", NSDate.date.timeIntervalSince1970, name]];
        return [fm moveItemAtURL:file toURL:dest error:err];
    };

    for (NSString* name in [names.allObjects sortedArrayUsingSelector:@selector(compare:)])
    {
        NSURL* l = [local URLByAppendingPathComponent:name];
        NSURL* c = [cloud URLByAppendingPathComponent:name];
        BOOL lExists = [fm fileExistsAtPath:l.path];
        BOOL cExists = [fm fileExistsAtPath:c.path] || [placeholders containsObject:name];

        if (cExists && ([placeholders containsObject:name] || (cloudReady && !cloudReady(c))))
        {
            count(@"skipped");  // iCloud has not downloaded it yet; decide next round
            continue;
        }
        if (lExists && settle > 0 && -[ManosabaModDate(l) timeIntervalSinceNow] < settle)
        {
            count(@"skipped");  // the game may still be writing it
            continue;
        }

        NSError* error = nil;
        NSData* lData = lExists ? [NSData dataWithContentsOfURL:l] : nil;
        NSData* cData = cExists ? ManosabaReadCloud(c, &error) : nil;
        if (cExists && !cData)
        {
            printf("[icloud] read failed %s: %s\n", name.UTF8String, error.localizedDescription.UTF8String);
            count(@"errors");
            continue;
        }
        NSString* lHash = lData ? ManosabaHash(lData) : nil;
        NSString* cHash = cData ? ManosabaHash(cData) : nil;
        NSString* mHash = manifest[name];

        if ((lHash == cHash) || [lHash isEqualToString:cHash])
        {
            if (lHash) manifest[name] = lHash; else [manifest removeObjectForKey:name];
            continue;
        }
        BOOL lChanged = !((lHash == mHash) || [lHash isEqualToString:mHash]);
        BOOL cChanged = !((cHash == mHash) || [cHash isEqualToString:mHash]);
        BOOL useLocal;
        if (lChanged && cChanged)
        {
            count(@"conflicts");
            // A file that still exists beats a deletion; otherwise the newer edit wins.
            if (!lData) useLocal = NO;
            else if (!cData) useLocal = YES;
            else if (firstSync) useLocal = NO;
            else useLocal = [ManosabaModDate(l) compare:ManosabaModDate(c)] != NSOrderedAscending;
            printf("[icloud] conflict %s: keeping %s copy\n", name.UTF8String, useLocal ? "local" : "iCloud");
        }
        else
            useLocal = lChanged;

        BOOL ok;
        if (useLocal)
        {
            if (lData) { ok = ManosabaWrite(lData, ManosabaModDate(l), c, YES, &error); if (ok) count(@"pushed"); }
            else { ok = ManosabaRemove(c, YES, &error); if (ok) count(@"deletedCloud"); }
            if (ok) { if (lHash) manifest[name] = lHash; else [manifest removeObjectForKey:name]; }
        }
        else
        {
            if (mode == ManosabaSyncModePushOnly || (allowLocalWrites && !allowLocalWrites()))
            {
                count(@"skipped");  // pull on the next launch
                continue;
            }
            if (cData)
            {
                // A local edit that lost a conflict is kept in the trash folder, not overwritten.
                ok = !(lData && lChanged) || toTrash(l, name, &error);
                ok = ok && ManosabaWrite(cData, ManosabaModDate(c), l, NO, &error);
                if (ok) count(@"pulled");
            }
            else
            {
                ok = toTrash(l, name, &error);
                if (ok) count(@"deletedLocal");
            }
            if (ok) { if (cHash) manifest[name] = cHash; else [manifest removeObjectForKey:name]; }
        }
        if (!ok)
        {
            printf("[icloud] %s %s failed: %s\n", useLocal ? "push" : "pull", name.UTF8String, error.localizedDescription.UTF8String);
            count(@"errors");
        }
    }

    NSData* out = [NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingSortedKeys error:nil];
    [fm createDirectoryAtURL:manifestURL.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [out writeToURL:manifestURL atomically:YES];
    return stats;
}
