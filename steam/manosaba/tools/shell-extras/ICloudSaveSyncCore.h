// Manosaba: two-way save sync between the game's local save folder and an iCloud folder.
// Platform-neutral Foundation code (unit-tested on macOS); ICloudSaveSync.mm wires it into the app.
#pragma once
#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, ManosabaSyncMode) {
    ManosabaSyncModeFull,      // at launch, before the game reads its saves: pull and push
    ManosabaSyncModePushOnly,  // while the game runs: never touch local files
};

// Syncs every *.nson file (save slots, quick/auto saves, GlobalSave) and Settings.json between `local` and `cloud`.
// The manifest remembers each file's hash at the last sync, so a change on one side is copied
// over and a deletion is propagated; when both sides changed, the newer modification date wins.
// `cloudReady` says whether a cloud file's contents are on this device (iCloud may still be
// downloading it); names that are not ready are skipped this round. `allowLocalWrites` is
// checked before every local write so the caller can stop a pull that ran too long.
// `settle`: local files modified less than this many seconds ago are skipped (being written).
// Returns counts: pulled, pushed, deletedLocal, deletedCloud, conflicts, skipped, errors.
NSDictionary<NSString*, NSNumber*>* ManosabaSyncSaves(NSURL* local, NSURL* cloud, NSURL* manifest,
                                                     ManosabaSyncMode mode, NSTimeInterval settle,
                                                     BOOL (^cloudReady)(NSURL* url),
                                                     BOOL (^allowLocalWrites)(void));

// Whether a file in the save folder takes part in the sync.
extern "C" BOOL ManosabaIsSyncedName(NSString* name);
