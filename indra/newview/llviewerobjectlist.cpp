/**
 * @file llviewerobjectlist.cpp
 * @brief Implementation of LLViewerObjectList class.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llviewerobjectlist.h"

#include "message.h"
#include "llfasttimer.h"
#include "llrender.h"
#include "llwindow.h"       // decBusyCount()

#include "llviewercontrol.h"
#include "llface.h"
#include "llvoavatar.h"
#include "llviewerobject.h"
#include "llviewerwindow.h"
#include "llnetmap.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "lltooltip.h"
#include "llworld.h"
#include "llstring.h"
#include "llhudicon.h"
#include "llhudnametag.h"
#include "lldrawable.h"
#include "llflexibleobject.h"
#include "llviewertextureanim.h"
#include "xform.h"
#include "llsky.h"
#include "llviewercamera.h"
#include "llselectmgr.h"
#include "llresmgr.h"
#include "llsdutil.h"
#include "llviewerregion.h"
#include "llviewerstats.h"
#include "llviewerstatsrecorder.h"
#include "llvovolume.h"
#include "llvoavatarself.h"
#include "lltoolmgr.h"
#include "lltoolpie.h"
#include "llkeyboard.h"
#include "u64.h"
#include "llviewertexturelist.h"
#include "lldatapacker.h"
#ifdef LL_USESYSTEMLIBS
#include <zlib.h>
#else
#include "zlib-ng/zlib.h"
#endif
#include "object_flags.h"

#include "llappviewer.h"
#include "llfloaterperms.h"
#include "llvocache.h"
#include "llcorehttputil.h"
#include "llstartup.h"

#include <algorithm>
#include <iterator>
#include <tuple> // <FS:Beq/> FIRE-30694 DeadObject Spamming cleanup
#include "fsassetblacklist.h"
#include "fsfloaterimport.h"
#include "fscommon.h"
#include "llfloaterreg.h"

#include "fsareasearch.h" // <FS:Cron> Added to provide the ability to update the impact costs in area search. </FS:Cron>
#include "llavataractions.h"

extern F32 gMinObjectDistance;
extern bool gAnimateTextures;

#define MAX_CONCURRENT_PHYSICS_REQUESTS 256

void dialog_refresh_all();

// Global lists of objects - should go away soon.
LLViewerObjectList gObjectList;

extern LLPipeline   gPipeline;

// Statics for object lookup tables.
U32                     LLViewerObjectList::sSimulatorMachineIndex = 1; // Not zero deliberately, to speed up index check.

LLViewerObjectList::LLViewerObjectList()
    : mNewObjectSignal() // <FS:Ansariel> FIRE-16647: Default object properties randomly aren't applied
{
    mCurLazyUpdateIndex = 0;
    mCurBin = 0;
    mNumDeadObjects = 0;
    mNumOrphans = 0;
    mNumNewObjects = 0;
    mWasPaused = false;
    mNumDeadObjectUpdates = 0;
    mNumUnknownUpdates = 0;
}

LLViewerObjectList::~LLViewerObjectList()
{
    destroy();
}

void LLViewerObjectList::destroy()
{
    killAllObjects();

    resetObjectBeacons();
    mActiveObjects.clear();
    mDeadObjects.clear();
    mMapObjects.clear();
    mUUIDObjectMap.clear();
}


void LLViewerObjectList::getUUIDFromLocal(LLUUID &id,
                                          const U32 local_id,
                                          const U32 ip,
                                          const U32 port)
{
    U64 ipport = (((U64)ip) << 32) | (U64)port;

    U32 index = mIPAndPortToIndex[ipport];

    if (!index)
    {
        index = sSimulatorMachineIndex++;
        mIPAndPortToIndex[ipport] = index;
    }

    U64 indexid = (((U64)index) << 32) | (U64)local_id;

    id = get_if_there(mIndexAndLocalIDToUUID, indexid, LLUUID::null);
}

U64 LLViewerObjectList::getIndex(const U32 local_id,
                                 const U32 ip,
                                 const U32 port)
{
    U64 ipport = (((U64)ip) << 32) | (U64)port;

    U32 index = mIPAndPortToIndex[ipport];

    if (!index)
    {
        return 0;
    }

    return (((U64)index) << 32) | (U64)local_id;
}

bool LLViewerObjectList::removeFromLocalIDTable(LLViewerObject* objectp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;

    if(objectp && objectp->mRegionIndex != 0)
    {
        U32 local_id = objectp->mLocalID;
        U64 indexid = (((U64)objectp->mRegionIndex) << 32) | (U64)local_id;

        std::map<U64, LLUUID>::iterator iter = mIndexAndLocalIDToUUID.find(indexid);
        if (iter == mIndexAndLocalIDToUUID.end())
        {
            return false;
        }

        // Found existing entry
        if (iter->second == objectp->getID())
        {   // Full UUIDs match, so remove the entry
            mIndexAndLocalIDToUUID.erase(iter);
            objectp->mRegionIndex = 0;
            return true;
        }
        // UUIDs did not match - this would zap a valid entry, so don't erase it
        //LL_INFOS() << "Tried to erase entry where id in table ("
        //      << iter->second << ") did not match object " << object.getID() << LL_ENDL;
    }

    return false ;
}

void LLViewerObjectList::setUUIDAndLocal(const LLUUID &id,
                                          const U32 local_id,
                                          const U32 ip,
                                          const U32 port,
                                          LLViewerObject* objectp)
{
    U64 ipport = (((U64)ip) << 32) | (U64)port;

    U32 index = mIPAndPortToIndex[ipport];

    if (!index)
    {
        index = sSimulatorMachineIndex++;
        mIPAndPortToIndex[ipport] = index;
    }

    objectp->mRegionIndex = index; // should never be zero, sSimulatorMachineIndex starts from 1
    U64 indexid = (((U64)index) << 32) | (U64)local_id;

    mIndexAndLocalIDToUUID[indexid] = id;

    //LL_INFOS() << "Adding object to table, full ID " << id
    //  << ", local ID " << local_id << ", ip " << ip << ":" << port << LL_ENDL;
}

S32 gFullObjectUpdates = 0;
S32 gTerseObjectUpdates = 0;

// <FS:CR> Object Import
boost::signals2::connection LLViewerObjectList::setNewObjectCallback(new_object_callback_t cb)
{
    return mNewObjectSignal.connect(cb);
}
// </FS:CR>

void LLViewerObjectList::processUpdateCore(LLViewerObject* objectp,
                                           void** user_data,
                                           U32 i,
                                           const EObjectUpdateType update_type,
                                           LLDataPacker* dpp,
                                           bool just_created,
                                           bool from_cache)
{
    LLMessageSystem* msg = NULL;

    if(!from_cache)
    {
        msg = gMessageSystem;
    }

    // ignore returned flags
    LL_DEBUGS("ObjectUpdate") << "uuid " << objectp->mID << " calling processUpdateMessage "
                              << objectp << " just_created " << just_created << " from_cache " << from_cache << " msg " << msg << LL_ENDL;

    objectp->processUpdateMessage(msg, user_data, i, update_type, dpp);

    if (objectp->isDead())
    {
        // The update failed
        return;
    }

    updateActive(objectp);

    if (just_created)
    {
        gPipeline.addObject(objectp);
    }

    // Also sets the approx. pixel area
    objectp->setPixelAreaAndAngle(gAgent);

    // RN: this must be called after we have a drawable
    // (from gPipeline.addObject)
    // so that the drawable parent is set properly
    if(msg != NULL)
    {
    findOrphans(objectp, msg->getSenderIP(), msg->getSenderPort());
    }
    else
    {
        LLViewerRegion* regionp = objectp->getRegion();
        if(regionp != NULL)
        {
            findOrphans(objectp, regionp->getHost().getAddress(), regionp->getHost().getPort());
        }
    }

    // If we're just wandering around, don't create new objects selected.
    if (just_created
        && update_type != OUT_TERSE_IMPROVED
        && objectp->mCreateSelected)
    {
        // <FS:Techwolf Lupindo> import support
        bool import_handled = false;
        bool own_full_perm = (objectp->permYouOwner() && objectp->permModify() && objectp->permTransfer() && objectp->permCopy());
        if (own_full_perm && !mNewObjectSignal.empty())
        {
            import_handled = mNewObjectSignal(objectp).get();
            mNewObjectSignal.disconnect_all_slots();
        }
        if (!import_handled)
        {
            if (own_full_perm && (FSCommon::sObjectAddMsg > 0))
            {
                FSCommon::sObjectAddMsg--;
                FSCommon::applyDefaultBuildPreferences(objectp);
            }

            if ( LLToolMgr::getInstance()->getCurrentTool() != LLToolPie::getInstance() )
            {
                // LL_INFOS() << "DEBUG selecting " << objectp->mID << " "
                // << objectp->mLocalID << LL_ENDL;
                LLSelectMgr::getInstance()->selectObjectAndFamily(objectp);
                dialog_refresh_all();
            }
        }
        // <FS:Techwolf Lupindo>
        objectp->mCreateSelected = false;
        gViewerWindow->getWindow()->decBusyCount();
        gViewerWindow->setCursor( UI_CURSOR_ARROW );
    }
}

static LLTrace::BlockTimerStatHandle FTM_PROCESS_OBJECTS("Process Objects");

LLViewerObject* LLViewerObjectList::processObjectUpdateFromCache(LLVOCacheEntry* entry, LLViewerRegion* regionp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;

    LLDataPacker *cached_dpp = entry->getDP();

    if (!cached_dpp || gNonInteractive)
    {
        return NULL; //nothing cached.
    }

    LLViewerObject *objectp;
    U32             local_id;
    LLPCode         pcode = 0;
    LLUUID          fullid;
    LLViewerStatsRecorder& recorder = LLViewerStatsRecorder::instance();

    // Cache Hit.
    record(LLStatViewer::OBJECT_CACHE_HIT_RATE, LLUnits::Ratio::fromValue(1));

    cached_dpp->reset();
    cached_dpp->unpackUUID(fullid, "ID");
    cached_dpp->unpackU32(local_id, "LocalID");
    cached_dpp->unpackU8(pcode, "PCode");

    // <FS:Ansariel> Don't process derendered objects
    if (mDerendered.end() != mDerendered.find(fullid))
    {
        return NULL;
    }
    // </FS:Ansariel>

    // <FS:Ansariel> FIRE-20288: Option to render friends only
    if (isNonFriendDerendered(fullid, pcode))
    {
        return NULL;
    }
    // </FS:Ansariel>

    objectp = findObject(fullid);

    if (objectp)
    {
        if(!objectp->isDead() && (objectp->mLocalID != entry->getLocalID() ||
            objectp->getRegion() != regionp))
        {
            removeFromLocalIDTable(objectp);
            setUUIDAndLocal(fullid, entry->getLocalID(),
                            regionp->getHost().getAddress(),
                            regionp->getHost().getPort(),
                            objectp);

            if (objectp->mLocalID != entry->getLocalID())
            {   // Update local ID in object with the one sent from the region
                objectp->mLocalID = entry->getLocalID();
            }

            if (objectp->getRegion() != regionp)
            {   // Object changed region, so update it
                objectp->updateRegion(regionp); // for LLVOAvatar
            }
        }
        else
        {
            //should fall through if already loaded because may need to update the object.
            //return objectp; //already loaded.
        }
    }

    bool justCreated = false;
    if (!objectp)
    {

        objectp = createObjectFromCache(pcode, regionp, fullid, entry->getLocalID());

        LL_DEBUGS("ObjectUpdate") << "uuid " << fullid << " created objectp " << objectp << LL_ENDL;

        if (!objectp)
        {
            LL_INFOS() << "createObject failure for object: " << fullid << LL_ENDL;
            recorder.objectUpdateFailure();
            return NULL;
        }
        justCreated = true;
        mNumNewObjects++;
    }

    if (objectp->isDead())
    {
        LL_WARNS() << "Dead object " << objectp->mID << " in UUID map 1!" << LL_ENDL;
    }

    processUpdateCore(objectp, NULL, 0, OUT_FULL_CACHED, cached_dpp, justCreated, true);
    objectp->loadFlags(entry->getUpdateFlags()); //just in case, reload update flags from cache.

    if(entry->getHitCount() > 0)
    {
        objectp->setLastUpdateType(OUT_FULL_CACHED);
    }
    else
    {
        objectp->setLastUpdateType(OUT_FULL_COMPRESSED); //newly cached
        objectp->setLastUpdateCached(true);
    }
    LLVOAvatar::cullAvatarsByPixelArea();

    return objectp;
}

void LLViewerObjectList::processObjectUpdate(LLMessageSystem *mesgsys,
                                             void **user_data,
                                             const EObjectUpdateType update_type,
                                             bool compressed)
{
    LL_RECORD_BLOCK_TIME(FTM_PROCESS_OBJECTS);

    LLViewerObject *objectp;
    S32         num_objects;
    U32         local_id = 0;
    LLPCode     pcode = 0;
    LLUUID      fullid;
    S32         i;

    // figure out which simulator these are from and get it's index
    // Coordinates in simulators are region-local
    // Until we get region-locality working on viewer we
    // have to transform to absolute coordinates.
    num_objects = mesgsys->getNumberOfBlocksFast(_PREHASH_ObjectData);

    // I don't think this case is ever hit.  TODO* Test this.
    if (!compressed && update_type != OUT_FULL)
    {
        //LL_INFOS() << "TEST: !cached && !compressed && update_type != OUT_FULL" << LL_ENDL;
        gTerseObjectUpdates += num_objects;
        /*
        S32 size;
        if (mesgsys->getReceiveCompressedSize())
        {
            size = mesgsys->getReceiveCompressedSize();
        }
        else
        {
            size = mesgsys->getReceiveSize();
        }
        LL_INFOS() << "Received terse " << num_objects << " in " << size << " byte (" << size/num_objects << ")" << LL_ENDL;
        */
    }
    else
    {
        /*
        S32 size;
        if (mesgsys->getReceiveCompressedSize())
        {
            size = mesgsys->getReceiveCompressedSize();
        }
        else
        {
            size = mesgsys->getReceiveSize();
        }

        LL_INFOS() << "Received " << num_objects << " in " << size << " byte (" << size/num_objects << ")" << LL_ENDL;
        */
        gFullObjectUpdates += num_objects;
    }

    U64 region_handle;
    mesgsys->getU64Fast(_PREHASH_RegionData, _PREHASH_RegionHandle, region_handle);

    LLViewerRegion *regionp = LLWorld::getInstance()->getRegionFromHandle(region_handle);

    if (!regionp)
    {
        LL_WARNS() << "Object update from unknown region! " << region_handle << LL_ENDL;
        return;
    }

    U8 compressed_dpbuffer[2048];
    LLDataPackerBinaryBuffer compressed_dp(compressed_dpbuffer, 2048);
    LLViewerStatsRecorder& recorder = LLViewerStatsRecorder::instance();

    for (i = 0; i < num_objects; i++)
    {
        bool justCreated = false;
        bool update_cache = false; //update object cache if it is a full-update or terse update

        if (compressed)
        {
            compressed_dp.reset();

            S32 uncompressed_length = mesgsys->getSizeFast(_PREHASH_ObjectData, i, _PREHASH_Data);
            LL_DEBUGS("ObjectUpdate") << "got binary data from message to compressed_dpbuffer" << LL_ENDL;
            mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_Data, compressed_dpbuffer, 0, i, 2048);
            compressed_dp.assignBuffer(compressed_dpbuffer, uncompressed_length);

            if (update_type != OUT_TERSE_IMPROVED) // OUT_FULL_COMPRESSED only?
            {
                U32 flags = 0;
                mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, i);

                compressed_dp.unpackUUID(fullid, "ID");
                compressed_dp.unpackU32(local_id, "LocalID");
                compressed_dp.unpackU8(pcode, "PCode");

                if (pcode == 0)
                {
                    // object creation will fail, LLViewerObject::createObject()
                    LL_WARNS() << "Received object " << fullid
                        << " with 0 PCode. Local id: " << local_id
                        << " Flags: " << flags
                        << " Region: " << regionp->getName()
                        << " Region id: " << regionp->getRegionID() << LL_ENDL;
                    recorder.objectUpdateFailure();
                    continue;
                }
                else if ((flags & FLAGS_TEMPORARY_ON_REZ) == 0)
                {
                    //send to object cache
                    regionp->cacheFullUpdate(compressed_dp, flags);
                    continue;
                }
            }
            else //OUT_TERSE_IMPROVED
            {
                update_cache = true;
                compressed_dp.unpackU32(local_id, "LocalID");
                getUUIDFromLocal(fullid,
                                 local_id,
                                 gMessageSystem->getSenderIP(),
                                 gMessageSystem->getSenderPort());
                if (fullid.isNull())
                {
                    LL_DEBUGS() << "update for unknown localid " << local_id << " host " << gMessageSystem->getSender() << ":" << gMessageSystem->getSenderPort() << LL_ENDL;
                    mNumUnknownUpdates++;
                }
            }
        }
        else if (update_type != OUT_FULL) // !compressed, !OUT_FULL ==> OUT_FULL_CACHED only?
        {
            mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_ID, local_id, i);

            getUUIDFromLocal(fullid,
                            local_id,
                            gMessageSystem->getSenderIP(),
                            gMessageSystem->getSenderPort());
            if (fullid.isNull())
            {
                // LL_WARNS() << "update for unknown localid " << local_id << " host " << gMessageSystem->getSender() << LL_ENDL;
                mNumUnknownUpdates++;
            }
            else
            {
                LL_DEBUGS("ObjectUpdate") << "Non-full, non-compressed update, obj " << local_id << ", global ID " << fullid << " from " << mesgsys->getSender() << LL_ENDL;
            }
        }
        else // OUT_FULL only?
        {
            update_cache = true;
            mesgsys->getUUIDFast(_PREHASH_ObjectData, _PREHASH_FullID, fullid, i);
            mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_ID, local_id, i);
            LL_DEBUGS("ObjectUpdate") << "Full Update, obj " << local_id << ", global ID " << fullid << " from " << mesgsys->getSender() << LL_ENDL;
        }
        objectp = findObject(fullid);

        if (compressed)
        {
            LL_DEBUGS("ObjectUpdate") << "uuid " << fullid << " received compressed data from message (earlier in function)" << LL_ENDL;
        }
        LL_DEBUGS("ObjectUpdate") << "uuid " << fullid << " objectp " << objectp
                                     << " update_cache " << (S32) update_cache << " compressed " << compressed
                                     << " update_type "  << update_type << LL_ENDL;

        if(update_cache)
        {
            //update object cache if the object receives a full-update or terse update
            objectp = regionp->updateCacheEntry(local_id, objectp);
        }

        // This looks like it will break if the local_id of the object doesn't change
        // upon boundary crossing, but we check for region id matching later...
        // Reset object local id and region pointer if things have changed
        if (objectp &&
            ((objectp->mLocalID != local_id) ||
             (objectp->getRegion() != regionp)))
        {
            //if (objectp->getRegion())
            //{
            //  LL_INFOS() << "Local ID change: Removing object from table, local ID " << objectp->mLocalID
            //          << ", id from message " << local_id << ", from "
            //          << LLHost(objectp->getRegion()->getHost().getAddress(), objectp->getRegion()->getHost().getPort())
            //          << ", full id " << fullid
            //          << ", objects id " << objectp->getID()
            //          << ", regionp " << (U32) regionp << ", object region " << (U32) objectp->getRegion()
            //          << LL_ENDL;
            //}
            removeFromLocalIDTable(objectp);
            setUUIDAndLocal(fullid,
                            local_id,
                            gMessageSystem->getSenderIP(),
                            gMessageSystem->getSenderPort(),
                            objectp);

            if (objectp->mLocalID != local_id)
            {   // Update local ID in object with the one sent from the region
                objectp->mLocalID = local_id;
            }

            if (objectp->getRegion() != regionp)
            {   // Object changed region, so update it
                objectp->updateRegion(regionp); // for LLVOAvatar
            }
        }

        if (!objectp)
        {
            if (compressed)
            {
                if (update_type == OUT_TERSE_IMPROVED)
                {
                    // LL_INFOS() << "terse update for an unknown object (compressed):" << fullid << LL_ENDL;
                    recorder.objectUpdateFailure();
                    continue;
                }
            }
            else
            {
                if (update_type != OUT_FULL)
                {
                    //LL_INFOS() << "terse update for an unknown object:" << fullid << LL_ENDL;
                    recorder.objectUpdateFailure();
                    continue;
                }

                mesgsys->getU8Fast(_PREHASH_ObjectData, _PREHASH_PCode, pcode, i);

            }
#ifdef IGNORE_DEAD
            if (mDeadObjects.find(fullid) != mDeadObjects.end())
            {
                mNumDeadObjectUpdates++;
                //LL_INFOS() << "update for a dead object:" << fullid << LL_ENDL;
                recorder.objectUpdateFailure();
                continue;
            }
#endif

            if (FSAssetBlacklist::getInstance()->isBlacklisted(fullid, (pcode == LL_PCODE_LEGACY_AVATAR ? LLAssetType::AT_PERSON : LLAssetType::AT_OBJECT)))
            {
                LL_INFOS() << "Blacklisted " << (pcode == LL_PCODE_LEGACY_AVATAR ? "avatar" : "object") << " blocked." << LL_ENDL;
                continue;
            }

            // <FS:Ansariel> FIRE-20288: Option to render friends only
            if (isNonFriendDerendered(fullid, pcode))
            {
                LL_INFOS() << "Not rendering avatar " << fullid.asString() << " because it is not on the friend list" << LL_ENDL;
                continue;
            }
            // </FS:Ansariel>

            objectp = createObject(pcode, regionp, fullid, local_id, gMessageSystem->getSender());

            LL_DEBUGS("ObjectUpdate") << "creating object " << fullid << " result " << objectp << LL_ENDL;

            if (!objectp)
            {
                LL_INFOS() << "createObject failure for object: " << fullid << LL_ENDL;
                recorder.objectUpdateFailure();
                continue;
            }

            justCreated = true;
            mNumNewObjects++;
        }

        // Gah, why bother spamming the log with messages we can't do
        //  anything about?! -- TS
#if 0
        if (objectp->isDead())
        {
            LL_WARNS() << "Dead object " << objectp->mID << " in UUID map 1!" << LL_ENDL;
        }
#endif

        //bool bCached = false;
        if (compressed)
        {
            if (update_type != OUT_TERSE_IMPROVED) // OUT_FULL_COMPRESSED only?
            {
                objectp->mLocalID = local_id;
            }
            processUpdateCore(objectp, user_data, i, update_type, &compressed_dp, justCreated);

#if 0
            if (update_type != OUT_TERSE_IMPROVED) // OUT_FULL_COMPRESSED only?
            {
                U32 flags = 0;
                mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, i);

                if(!(flags & FLAGS_TEMPORARY_ON_REZ))
                {
                    bCached = true;
                    LLViewerRegion::eCacheUpdateResult result = objectp->mRegionp->cacheFullUpdate(objectp, compressed_dp, flags);
                    recorder.cacheFullUpdate(result);
                }
            }
#endif
        }
        else
        {
            if (update_type == OUT_FULL)
            {
                objectp->mLocalID = local_id;
            }
            processUpdateCore(objectp, user_data, i, update_type, NULL, justCreated);
        }
        recorder.objectUpdateEvent(update_type);
        objectp->setLastUpdateType(update_type);
    }

    LLVOAvatar::cullAvatarsByPixelArea();
}

void LLViewerObjectList::processCompressedObjectUpdate(LLMessageSystem *mesgsys,
                                             void **user_data,
                                             const EObjectUpdateType update_type)
{
    processObjectUpdate(mesgsys, user_data, update_type, true);
}

void LLViewerObjectList::processCachedObjectUpdate(LLMessageSystem *mesgsys,
                                             void **user_data,
                                             const EObjectUpdateType update_type)
{
    //processObjectUpdate(mesgsys, user_data, update_type, true, false);

    S32 num_objects = mesgsys->getNumberOfBlocksFast(_PREHASH_ObjectData);
    gFullObjectUpdates += num_objects;

    U64 region_handle;
    mesgsys->getU64Fast(_PREHASH_RegionData, _PREHASH_RegionHandle, region_handle);
    LLViewerRegion *regionp = LLWorld::getInstance()->getRegionFromHandle(region_handle);
    if (!regionp)
    {
        LL_WARNS() << "Object update from unknown region! " << region_handle << LL_ENDL;
        return;
    }

    LLViewerStatsRecorder& recorder = LLViewerStatsRecorder::instance();

    for (S32 i = 0; i < num_objects; i++)
    {
        U32 id;
        U32 crc;
        U32 flags;
        mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_ID, id, i);
        mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_CRC, crc, i);
        mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, i);

        LL_DEBUGS("ObjectUpdate") << "got probe for id " << id << " crc " << crc << LL_ENDL;

        // Lookup data packer and add this id to cache miss lists if necessary.
        U8 cache_miss_type = LLViewerRegion::CACHE_MISS_TYPE_NONE;
        if (regionp->probeCache(id, crc, flags, cache_miss_type))
        {   // Cache Hit
            recorder.cacheHitEvent();
        }
        else
        {   // Cache Miss
            LL_DEBUGS("ObjectUpdate") << "cache miss for id " << id << " crc " << crc << " miss type " << (S32) cache_miss_type << LL_ENDL;
            recorder.cacheMissEvent(cache_miss_type);
        }
    }

    return;
}

void LLViewerObjectList::dirtyAllObjectInventory()
{
    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        (*iter)->dirtyInventory();
    }
}

// [SL:KB] - Patch: Render-TextureToggle (Catznip-4.0)
void LLViewerObjectList::setAllObjectDefaultTextures(U32 nChannel, bool fShowDefault)
{
    LLPipeline::sRenderTextures = !fShowDefault;

    for (LLViewerObject* pObj : mObjects)
    {
        LLDrawable* pDrawable = pObj->mDrawable;
        if ( (pDrawable) && (!pDrawable->isDead()) )
        {
            for (int idxFace = 0, cntFace = pDrawable->getNumFaces(); idxFace < cntFace; idxFace++)
            {
                if (LLFace* pFace = pDrawable->getFace(idxFace))
                    pFace->setDefaultTexture(nChannel, fShowDefault);
            }

            if (LLVOVolume* pVoVolume = pDrawable->getVOVolume())
                pVoVolume->markForUpdate();
        }
    }
}
// [/SL:KB]
// <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
//void LLViewerObjectList::updateApparentAngles(LLAgent &agent)
// Added time limit on processing of objects as they affect the texture system (They also calcuate mMaxVirtualSize and mPixelArea)
void LLViewerObjectList::updateApparentAngles(LLAgent &agent, F32 max_time)
// </FS:minerjr> [FIRE-35081]
{
    S32 i;
    LLViewerObject *objectp;

    S32 num_updates, max_value;
    // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
    // Remove the old code as it worked on fixed number of updates (Total # of Object / 128) per frame
    // and some objects had nothing to do while others were avatars or volumes and could t
    /*
    if (NUM_BINS - 1 == mCurBin)
    {
        // Remainder (mObjects.size() could have changed)
        num_updates = (S32) mObjects.size() - mCurLazyUpdateIndex;
        max_value = (S32) mObjects.size();
    }
    else
    {
        num_updates = ((S32) mObjects.size() / NUM_BINS) + 1;
        max_value = llmin((S32) mObjects.size(), mCurLazyUpdateIndex + num_updates);
    }

    // Iterate through some of the objects and lazy update their texture priorities
    for (i = mCurLazyUpdateIndex; i < max_value; i++)
    {
        objectp = mObjects[i];
        if (!objectp->isDead())
        {
            //  Update distance & gpw
            objectp->setPixelAreaAndAngle(agent); // Also sets the approx. pixel area
            objectp->updateTextures();  // Update the image levels of textures for this object.
        }
    }

    mCurLazyUpdateIndex = max_value;
    if (mCurLazyUpdateIndex == mObjects.size())
    {
        // restart
        mCurLazyUpdateIndex = 0;
        mCurBin = 0; // keep in sync with index (mObjects.size() could have changed)
    }
    else
    {
        mCurBin = (mCurBin + 1) % NUM_BINS;
    }
    */
    num_updates = 0;    
    max_value = (S32)mObjects.size();
    LLTimer timer;
    // If the number of objects since last being in here has changed (IE objects deleted, then reset the lazy update index)
    if (mCurLazyUpdateIndex >= max_value)
    {
        mCurLazyUpdateIndex = 0;
    }
    // Store the index for the current lazy update index as we will loop over the index
    i = mCurLazyUpdateIndex;    
    // loop over number of objects in the BIN (128), or below until we run out of time
    while(num_updates < NUM_BINS)
    {
        // Moved to the first to fix up the issue of access violation if the object list chaanges size during processing.
        if (i >= (S32)mObjects.size())
        {
            // Reset the index if we go over the max value
            i = 0;
        }
        objectp = mObjects[i];
        if (objectp != nullptr && !objectp->isDead())
        {
            //LL_DEBUGS() << objectp->getID() << " Update Textures" << LL_ENDL;
            //  Update distance & gpw
            objectp->setPixelAreaAndAngle(agent); // Also sets the approx. pixel area
            //TommyTheTerrible - We are updating the texture levels way too much so limiting to only avatars.
            if (objectp->isAvatar())
                objectp->updateTextures();  // Update the image levels of textures for this object.
        }
        i++;    

        num_updates++;
        // Escape either if we run out of time, or loop back onto ourselves.
        if (timer.getElapsedTimeF32() > max_time || i == mCurLazyUpdateIndex)
        {
            break;
        }
    }
    // Update the current lazy update index with the current index, so we can continue next frame from where we left off
    mCurLazyUpdateIndex = i;
    // </FS:minerjr> [FIRE-35081]
#if 0
    // Slam priorities for textures that we care about (hovered, selected, and focused)
    // Hovered
    // Assumes only one level deep of parenting
    LLSelectNode* nodep = LLSelectMgr::instance().getHoverNode();
    if (nodep)
    {
        objectp = nodep->getObject();
        if (objectp)
        {
            objectp->boostTexturePriority();
        }
    }

    // Focused
    objectp = gAgentCamera.getFocusObject();
    if (objectp)
    {
        objectp->boostTexturePriority();
    }
#endif

    // Selected
    struct f : public LLSelectedObjectFunctor
    {
        virtual bool apply(LLViewerObject* objectp)
        {
            if (objectp)
            {
                objectp->boostTexturePriority();
            }
            return true;
        }
    } func;
    LLSelectMgr::getInstance()->getSelection()->applyToRootObjects(&func);

    LLVOAvatar::cullAvatarsByPixelArea();
}

static LLTrace::BlockTimerStatHandle FTM_ACTIVE_OBJECT_UPDATES("Active Object Updates");

void LLViewerObjectList::update(LLAgent &agent)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    LL_RECORD_BLOCK_TIME(FTM_ACTIVE_OBJECT_UPDATES);
    // <FS:Ansariel> Speed up debug settings
    static LLCachedControl<bool> velocityInterpolate(gSavedSettings, "VelocityInterpolate");
    static LLCachedControl<bool> pingInterpolate(gSavedSettings, "PingInterpolate");
    static LLCachedControl<F32> interpolationTime(gSavedSettings, "InterpolationTime");
    static LLCachedControl<F32> interpolationPhaseOut(gSavedSettings, "InterpolationPhaseOut");
    static LLCachedControl<F32> regionCrossingInterpolationTime(gSavedSettings, "RegionCrossingInterpolationTime");
    static LLCachedControl<bool> animateTextures(gSavedSettings, "AnimateTextures");
    static LLCachedControl<bool> freezeTime(gSavedSettings, "FreezeTime");
    // </FS:Ansariel> Speed up debug settings

    // Update globals
    // </FS:Ansariel> Speed up debug settings
    //LLViewerObject::setVelocityInterpolate( gSavedSettings.getBOOL("VelocityInterpolate") );
    //LLViewerObject::setPingInterpolate( gSavedSettings.getBOOL("PingInterpolate") );
    //
    //F32 interp_time = gSavedSettings.getF32("InterpolationTime");
    //F32 phase_out_time = gSavedSettings.getF32("InterpolationPhaseOut");
    //F32 region_interp_time = llclamp(gSavedSettings.getF32("RegionCrossingInterpolationTime"), 0.5f, 5.f);
    LLViewerObject::setVelocityInterpolate(velocityInterpolate);
    LLViewerObject::setPingInterpolate(pingInterpolate);

    F32 interp_time = (F32)interpolationTime;
    F32 phase_out_time = (F32)interpolationPhaseOut;
    F32 region_interp_time = llclamp(regionCrossingInterpolationTime(), 0.5f, 5.f);
    // </FS:Ansariel> Speed up debug settings
    if (interp_time < 0.0 ||
        phase_out_time < 0.0 ||
        phase_out_time > interp_time)
    {
        LL_WARNS() << "Invalid values for InterpolationTime or InterpolationPhaseOut, resetting to defaults" << LL_ENDL;
        interp_time = 3.0f;
        phase_out_time = 1.0f;
    }
    LLViewerObject::setPhaseOutUpdateInterpolationTime( interp_time );
    LLViewerObject::setMaxUpdateInterpolationTime( phase_out_time );
    LLViewerObject::setMaxRegionCrossingInterpolationTime(region_interp_time);

    // <FS:Ansariel> Speed up debug settings
    //gAnimateTextures = gSavedSettings.getBOOL("AnimateTextures");
    gAnimateTextures = animateTextures;
    // </FS:Ansariel> Speed up debug settings

    // update global timer
    F32 last_time = gFrameTimeSeconds;
    U64Microseconds time = totalTime();              // this will become the new gFrameTime when the update is done
    // Time _can_ go backwards, for example if the user changes the system clock.
    // It doesn't cause any fatal problems (just some oddness with stats), so we shouldn't assert here.
//  llassert(time > gFrameTime);
    F64Seconds time_diff = time - gFrameTime;
    gFrameTime  = time;
    F64Seconds time_since_start = gFrameTime - gStartTime;
    gFrameTimeSeconds = time_since_start;

    gFrameIntervalSeconds = gFrameTimeSeconds - last_time;
    if (gFrameIntervalSeconds < 0.f)
    {
        gFrameIntervalSeconds = 0.f;
    }

    //clear avatar LOD change counter
    LLVOAvatar::sNumLODChangesThisFrame = 0;

    const F64 frame_time = LLFrameTimer::getElapsedSeconds();

    LLViewerObject *objectp = NULL;

    // Make a copy of the list in case something in idleUpdate() messes with it
    static std::vector<LLViewerObject*> idle_list;

    U32 idle_count = 0;
    mNumAvatars = 0;

    {
        for (std::vector<LLPointer<LLViewerObject> >::iterator active_iter = mActiveObjects.begin();
            active_iter != mActiveObjects.end(); active_iter++)
        {
            objectp = *active_iter;
            if (objectp)
            {
                if (idle_count >= idle_list.size())
                {
                    idle_list.push_back( objectp );
                }
                else
                {
                    idle_list[idle_count] = objectp;
                }
                ++idle_count;
                if (objectp->isAvatar())
                {
                    mNumAvatars++;
                }
            }
            else
            {   // There shouldn't be any NULL pointers in the list, but they have caused
                // crashes before.  This may be idleUpdate() messing with the list.
                LL_WARNS() << "LLViewerObjectList::update has a NULL objectp" << LL_ENDL;
            }
        }
    }

    std::vector<LLViewerObject*>::iterator idle_end = idle_list.begin()+idle_count;

    // <FS:Ansariel> Speed up debug settings
    //if (gSavedSettings.getBOOL("FreezeTime"))
    if (freezeTime)
    // </FS:Ansariel> Speed up debug settings
    {

        for (std::vector<LLViewerObject*>::iterator iter = idle_list.begin();
            iter != idle_end; iter++)
        {
            objectp = *iter;
            if (objectp->isAvatar())
            {
                objectp->idleUpdate(agent, frame_time);
            }
        }
    }
    else
    {
        for (std::vector<LLViewerObject*>::iterator idle_iter = idle_list.begin();
            idle_iter != idle_end; idle_iter++)
        {
            objectp = *idle_iter;
            llassert(objectp->isActive());
                objectp->idleUpdate(agent, frame_time);
        }

        //update flexible objects
        LLVolumeImplFlexible::updateClass();

        //update animated textures
        if (gAnimateTextures)
        {
            LLViewerTextureAnim::updateClass();
        }
    }



    fetchObjectCosts();
    fetchPhysicsFlags();

    // update max computed render cost
    LLVOVolume::updateRenderComplexity();

    // compute all sorts of time-based stats
    // don't factor frames that were paused into the stats
    if (! mWasPaused)
    {
        LLViewerStats::getInstance()->updateFrameStats(time_diff);
    }

    /*
    // Debugging code for viewing orphans, and orphaned parents
    LLUUID id;
    for (i = 0; i < mOrphanParents.size(); i++)
    {
        id = sIndexAndLocalIDToUUID[mOrphanParents[i]];
        LLViewerObject *objectp = findObject(id);
        if (objectp)
        {
            std::string id_str;
            objectp->mID.toString(id_str);
            std::string tmpstr = std::string("Par:  ") + id_str;
            addDebugBeacon(objectp->getPositionAgent(),
                            tmpstr,
                            LLColor4(1.f,0.f,0.f,1.f),
                            LLColor4(1.f,1.f,1.f,1.f));
        }
    }

    LLColor4 text_color;
    for (i = 0; i < mOrphanChildren.size(); i++)
    {
        OrphanInfo oi = mOrphanChildren[i];
        LLViewerObject *objectp = findObject(oi.mChildInfo);
        if (objectp)
        {
            std::string id_str;
            objectp->mID.toString(id_str);
            std::string tmpstr;
            if (objectp->getParent())
            {
                tmpstr = std::string("ChP:  ") + id_str;
                text_color = LLColor4(0.f, 1.f, 0.f, 1.f);
            }
            else
            {
                tmpstr = std::string("ChNoP:    ") + id_str;
                text_color = LLColor4(1.f, 0.f, 0.f, 1.f);
            }
            id = sIndexAndLocalIDToUUID[oi.mParentInfo];
            addDebugBeacon(objectp->getPositionAgent() + LLVector3(0.f, 0.f, -0.25f),
                            tmpstr,
                            LLColor4(0.25f,0.25f,0.25f,1.f),
                            text_color);
        }
        i++;
    }
    */

    sample(LLStatViewer::NUM_OBJECTS, mObjects.size());
    sample(LLStatViewer::NUM_ACTIVE_OBJECTS, idle_count);
}

void LLViewerObjectList::fetchObjectCosts()
{
    // issue http request for stale object physics costs
    if (!mStaleObjectCost.empty())
    {
        // <FS:Ansariel> FIRE-5496: Missing LI for objects outside agent's region
        //LLViewerRegion* regionp = gAgent.getRegion();

        //if (regionp)
        //{
        //  std::string url = regionp->getCapability("GetObjectCost");

        //  if (!url.empty())
        //  {
  //              LLCoros::instance().launch("LLViewerObjectList::fetchObjectCostsCoro",
  //                  boost::bind(&LLViewerObjectList::fetchObjectCostsCoro, this, url));
        //  }
        //  else
        //  {
        //      mStaleObjectCost.clear();
        //      mPendingObjectCost.clear();
        //  }
        //}

        std::map<LLViewerRegion*, uuid_set_t> regionObjectMap{};

        // Swap it for thread safety since we're going to iterate over it
        uuid_set_t staleObjectCostIds{};
        staleObjectCostIds.swap(mStaleObjectCost);

        for (const auto& staleObjectId : staleObjectCostIds)
        {
            LLViewerObject* staleObject = findObject(staleObjectId);
            if (staleObject && staleObject->getRegion())
            {
                if (regionObjectMap.find(staleObject->getRegion()) == regionObjectMap.end())
                {
                    regionObjectMap.insert(std::make_pair(staleObject->getRegion(), uuid_set_t()));
                }

                regionObjectMap[staleObject->getRegion()].insert(staleObjectId);
            }
        }

        for (auto region : regionObjectMap)
        {
            std::string url = region.first->getCapability("GetObjectCost");

            if (!url.empty())
            {
                LLCoros::instance().launch("LLViewerObjectList::fetchObjectCostsCoro",
                    boost::bind(&LLViewerObjectList::fetchObjectCostsCoro, this, url, region.second));
            }
            else
            {
                for (const auto& objectId : region.second)
                {
                    mPendingObjectCost.erase(objectId);
                }
            }
        }
        // </FS:Ansariel>
    }
}

/*static*/
void LLViewerObjectList::reportObjectCostFailure(LLSD &objectList)
{
    // TODO*: No more hard coding
    for (LLSD::array_iterator it = objectList.beginArray(); it != objectList.endArray(); ++it)
    {
        gObjectList.onObjectCostFetchFailure(it->asUUID());
    }
}


// <FS:Ansariel> FIRE-5496: Missing LI for objects outside agent's region
//void LLViewerObjectList::fetchObjectCostsCoro(std::string url)
void LLViewerObjectList::fetchObjectCostsCoro(std::string url, uuid_set_t staleObjects)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter(new LLCoreHttpUtil::HttpCoroutineAdapter("genericPostCoro", httpPolicy));
    LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);



    uuid_set_t diff;

    // <FS:Ansariel> FIRE-5496: Missing LI for objects outside agent's region
    //std::set_difference(mStaleObjectCost.begin(), mStaleObjectCost.end(),
    //    mPendingObjectCost.begin(), mPendingObjectCost.end(),
    //    std::inserter(diff, diff.begin()));

    //mStaleObjectCost.clear();
    std::set_difference(staleObjects.begin(), staleObjects.end(),
        mPendingObjectCost.begin(), mPendingObjectCost.end(),
        std::inserter(diff, diff.begin()));
    // </FS:Ansariel>

    if (diff.empty())
    {
        LL_DEBUGS() << "No outstanding object IDs to request. Pending count: " << mPendingObjectCost.size() << LL_ENDL;
        return;
    }

    LLSD idList(LLSD::emptyArray());

    for (uuid_set_t::iterator it = diff.begin(); it != diff.end(); ++it)
    {
        idList.append(*it);
    }

    mPendingObjectCost.insert(diff.begin(), diff.end());

    LLSD postData = LLSD::emptyMap();

    postData["object_ids"] = idList;

    LLSD result = httpAdapter->postAndSuspend(httpRequest, url, postData);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status || result.has("error"))
    {
        if (result.has("error"))
        {
            LL_WARNS() << "Application level error when fetching object "
                << "cost.  Message: " << result["error"]["message"].asString()
                << ", identifier: " << result["error"]["identifier"].asString()
                << LL_ENDL;

            // TODO*: Adaptively adjust request size if the
            // service says we've requested too many and retry
        }
        reportObjectCostFailure(idList);

        return;
    }

    // Success, grab the resource cost and linked set costs
    // for an object if one was returned
    for (LLSD::array_iterator it = idList.beginArray(); it != idList.endArray(); ++it)
    {
        LLUUID objectId = it->asUUID();

        // Object could have been added to the mStaleObjectCost after request started
        mStaleObjectCost.erase(objectId);
        mPendingObjectCost.erase(objectId);

        // Check to see if the request contains data for the object
        if (result.has(it->asString()))
        {
            LLSD objectData = result[it->asString()];

            F32 linkCost = (F32)objectData["linked_set_resource_cost"].asReal();
            F32 objectCost = (F32)objectData["resource_cost"].asReal();
            F32 physicsCost = (F32)objectData["physics_cost"].asReal();
            F32 linkPhysicsCost = (F32)objectData["linked_set_physics_cost"].asReal();

            gObjectList.updateObjectCost(objectId, objectCost, linkCost, physicsCost, linkPhysicsCost);

            // <FS:Cron> area search
            // Update area search to have current information.
            FSAreaSearch* area_search_floater = LLFloaterReg::findTypedInstance<FSAreaSearch>("area_search");
            if (area_search_floater)
            {
                area_search_floater->updateObjectCosts(objectId, objectCost, linkCost, physicsCost, linkPhysicsCost);
            }
            // </FS:Cron> area search
        }
        else
        {
            // TODO*: Give user feedback about the missing data?
            gObjectList.onObjectCostFetchFailure(objectId);
        }
    }

}

void LLViewerObjectList::fetchPhysicsFlags()
{
    // issue http request for stale object physics flags
    if (!mStalePhysicsFlags.empty())
    {
        LLViewerRegion* regionp = gAgent.getRegion();

        if (regionp)
        {
            std::string url = regionp->getCapability("GetObjectPhysicsData");

            if (!url.empty())
            {
                LLCoros::instance().launch("LLViewerObjectList::fetchPhisicsFlagsCoro",
                    boost::bind(&LLViewerObjectList::fetchPhisicsFlagsCoro, this, url));
            }
            else
            {
                mStalePhysicsFlags.clear();
                mPendingPhysicsFlags.clear();
            }
        }
    }
}

/*static*/
void LLViewerObjectList::reportPhysicsFlagFailure(LLSD &objectList)
{
    // TODO*: No more hard coding
    for (LLSD::array_iterator it = objectList.beginArray(); it != objectList.endArray(); ++it)
    {
        gObjectList.onPhysicsFlagsFetchFailure(it->asUUID());
    }
}

void LLViewerObjectList::fetchPhisicsFlagsCoro(std::string url)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter(new LLCoreHttpUtil::HttpCoroutineAdapter("genericPostCoro", httpPolicy));
    LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);

    LLSD idList;
    U32 objectIndex = 0;

    for (uuid_set_t::iterator it = mStalePhysicsFlags.begin(); it != mStalePhysicsFlags.end();)
    {
        // Check to see if a request for this object
        // has already been made.
        if (mPendingPhysicsFlags.find(*it) == mPendingPhysicsFlags.end())
        {
            mPendingPhysicsFlags.insert(*it);
            idList[objectIndex++] = *it;
        }

        mStalePhysicsFlags.erase(it++);

        if (objectIndex >= MAX_CONCURRENT_PHYSICS_REQUESTS)
        {
            break;
        }
    }

    if (idList.size() < 1)
    {
        LL_DEBUGS() << "No outstanding object physics flags to request." << LL_ENDL;
        return;
    }

    LLSD postData = LLSD::emptyMap();

    postData["object_ids"] = idList;

    LLSD result = httpAdapter->postAndSuspend(httpRequest, url, postData);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status || result.has("error"))
    {
        if (result.has("error"))
        {
            LL_WARNS() << "Application level error when fetching object "
                << "physics flags.  Message: " << result["error"]["message"].asString()
                << ", identifier: " << result["error"]["identifier"].asString()
                << LL_ENDL;

            // TODO*: Adaptively adjust request size if the
            // service says we've requested too many and retry
        }
        reportPhysicsFlagFailure(idList);

        return;
    }

    // Success, grab the resource cost and linked set costs
    // for an object if one was returned
    for (LLSD::array_iterator it = idList.beginArray(); it != idList.endArray(); ++it)
    {
        LLUUID objectId = it->asUUID();

        // Check to see if the request contains data for the object
        if (result.has(it->asString()))
        {
            const LLSD& data = result[it->asString()];

            S32 shapeType = data["PhysicsShapeType"].asInteger();

            gObjectList.updatePhysicsShapeType(objectId, shapeType);

            if (data.has("Density"))
            {
                F32 density = (F32)data["Density"].asReal();
                F32 friction = (F32)data["Friction"].asReal();
                F32 restitution = (F32)data["Restitution"].asReal();
                F32 gravityMult = (F32)data["GravityMultiplier"].asReal();

                gObjectList.updatePhysicsProperties(objectId, density,
                    friction, restitution, gravityMult);
            }
        }
        else
        {
            // TODO*: Give user feedback about the missing data?
            gObjectList.onPhysicsFlagsFetchFailure(objectId);
        }
    }
}

void LLViewerObjectList::clearDebugText()
{
    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        (*iter)->restoreHudText();
    }
}


void LLViewerObjectList::cleanupReferences(LLViewerObject *objectp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    // <FS:Beq> FIRE-30694 DeadObject Spam - handle new_dead_object properly and closer to source
    // bool new_dead_object = true;
    if (mDeadObjects.find(objectp->mID) != mDeadObjects.end())
    {
    // <FS:Beq> FIRE-30694 DeadObject Spam
    // LL_INFOS() << "Object " << objectp->mID << " already on dead list!" << LL_ENDL;
    //  new_dead_object = false;
        LL_DEBUGS() << "Object " << objectp->mID << " already on dead list!" << LL_ENDL;
    // </FS:Beq>
    }
    // <FS:Beq> detect but still delete dupes
    // else
    {
    // <FS:Beq> FIRE-30694 DeadObject Spam
    //  mDeadObjects.insert(objectp->mID);
        mDeadObjects.insert( objectp->mID );
        mNumDeadObjects++;
        llassert( mNumDeadObjects == mDeadObjects.size() );
    // </FS:Beq>
    }

    // Cleanup any references we have to this object
    // Remove from object map so noone can look it up.

    LL_DEBUGS("ObjectUpdate") << " dereferencing id " << objectp->mID << LL_ENDL;

    mUUIDObjectMap.erase(objectp->mID);

    //if (objectp->getRegion())
    //{
    //  LL_INFOS() << "cleanupReferences removing object from table, local ID " << objectp->mLocalID << ", ip "
    //              << objectp->getRegion()->getHost().getAddress() << ":"
    //              << objectp->getRegion()->getHost().getPort() << LL_ENDL;
    //}

    removeFromLocalIDTable(objectp);

    if (objectp->onActiveList())
    {
        //LL_INFOS() << "Removing " << objectp->mID << " " << objectp->getPCodeString() << " from active list in cleanupReferences." << LL_ENDL;
        objectp->setOnActiveList(false);
        removeFromActiveList(objectp);
    }

    if (objectp->isOnMap())
    {
        removeFromMap(objectp);
    }

    // Don't clean up mObject references, these will be cleaned up more efficiently later!

    // <FS:Beq> FIRE-30694 DeadObject Spam
    // if(new_dead_object)
    // {
    //  mNumDeadObjects++;
    // }
}

bool LLViewerObjectList::killObject(LLViewerObject *objectp)
{
    LL_PROFILE_ZONE_SCOPED;
    // Don't ever kill gAgentAvatarp, just force it to the agent's region
    // unless region is NULL which is assumed to mean you are logging out.
    if ((objectp == gAgentAvatarp) && gAgent.getRegion())
    {
        objectp->setRegion(gAgent.getRegion());
        return false;
    }

    // When we're killing objects, all we do is mark them as dead.
    // We clean up the dead objects later.

    if (objectp)
    {
        // We are going to cleanup a lot of smart pointers to this object, they might be last,
        // and object being NULLed while inside it's own function won't be pretty
        // so create a pointer to make sure object will stay alive untill markDead() finishes
        LLPointer<LLViewerObject> sp(objectp);
        sp->markDead(); // does the right thing if object already dead
        return true;
    }

    return false;
}

// <FS:Beq> Animated Objects kill switch
void LLViewerObjectList::killAnimatedObjects()
{
    LLViewerObject *objectp;

    for (auto iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        objectp = *iter;

        if (objectp->isAnimatedObject())
        {
            killObject(objectp);
            if (LLViewerRegion::sVOCacheCullingEnabled && objectp->getRegion())
            {
                objectp->getRegion()->killCacheEntry(objectp->getLocalID());
            }
        }
    }

    cleanDeadObjects(false);
}
// </FS:Beq>

void LLViewerObjectList::killObjects(LLViewerRegion *regionp)
{
    LL_PROFILE_ZONE_SCOPED;
    LLViewerObject *objectp;


    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        objectp = *iter;

        if (objectp->mRegionp == regionp)
        {
            killObject(objectp);
        }
    }

    // Have to clean right away because the region is becoming invalid.
    cleanDeadObjects(false);
}

void LLViewerObjectList::killAllObjects()
{
    // Used only on global destruction.

    // Mass cleanup to not clear lists one item at a time
    mIndexAndLocalIDToUUID.clear();
    mActiveObjects.clear();
    mMapObjects.clear();

    LLViewerObject *objectp;
    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        objectp = *iter;
        objectp->setOnActiveList(false);
        objectp->setListIndex(-1);
        objectp->mRegionIndex = 0;
        objectp->mOnMap = false;
        killObject(objectp);
        // Object must be dead, or it's the LLVOAvatarSelf which never dies.
        llassert((objectp == gAgentAvatarp) || objectp->isDead());
    }

    cleanDeadObjects(false);

    if(!mObjects.empty())
    {
        LL_WARNS() << "LLViewerObjectList::killAllObjects still has entries in mObjects: " << mObjects.size() << LL_ENDL;
        mObjects.clear();
    }
}

void LLViewerObjectList::cleanDeadObjects(bool use_timer)
{
    // <FS:Beq/> FIRE-30694 DeadObject Spam
    llassert( mNumDeadObjects == mDeadObjects.size() );

    if (!mNumDeadObjects)
    {
        // No dead objects, don't need to scan object list.
        return;
    }

    LL_PROFILE_ZONE_SCOPED;

    // <FS:Beq/> FIRE-30694 DeadObject Spam
    S32 num_divergent = 0;
    S32 num_removed = 0;
    LLViewerObject *objectp;

    // <FS:Ansariel> Use timer for cleaning up dead objects
    static const F64 max_time = 0.01; // Let's try 10ms per frame
    LLTimer timer;
    // </FS:Ansariel>

    vobj_list_t::reverse_iterator target = mObjects.rbegin();

    vobj_list_t::iterator iter = mObjects.begin();
    for ( ; iter != mObjects.end(); )
    {
        // Scan for all of the dead objects and put them all on the end of the list with no ref count ops
        objectp = *iter;
        if (objectp == NULL)
        { //we caught up to the dead tail
            break;
        }

        if (objectp->isDead())
        {
            // <FS:Beq> FIRE-30694 DeadObject Spam
            // mDeadObjects.erase(objectp->mID); // <FS:Ansariel> Use timer for cleaning up dead objects
            auto delete_me = mDeadObjects.find(objectp->mID);
            if( delete_me !=mDeadObjects.end() )
            {
                mDeadObjects.erase( delete_me );
            }
            else
            {
                LL_WARNS() << "Attempt to delete object " << objectp->mID << " but object not in dead list" << LL_ENDL;
                num_divergent++; // this is the number we are adrift in the count
            }

            LLPointer<LLViewerObject>::swap(*iter, *target);
            *target = NULL;
            ++target;
            num_removed++;

            // <FS:Ansariel> Use timer for cleaning up dead objects
            //if (num_removed == mNumDeadObjects || iter->isNull())
            if (num_removed == mNumDeadObjects || iter->isNull() || (use_timer && timer.getElapsedTimeF64() > max_time))
            // </FS:Ansariel>
            {
                // We've cleaned up all of the dead objects or caught up to the dead tail
                break;
            }
        }
        else
        {
            ++iter;
        }
    }

    // <FS:Ansariel> Use timer for cleaning up dead objects
    //llassert(num_removed == mNumDeadObjects);

    ////erase as a block
    //mObjects.erase(mObjects.begin()+(mObjects.size()-mNumDeadObjects), mObjects.end());

    //// We've cleaned the global object list, now let's do some paranoia testing on objects
    //// before blowing away the dead list.
    //mDeadObjects.clear();
    //mNumDeadObjects = 0;
    mObjects.erase(mObjects.begin()+(mObjects.size()-num_removed), mObjects.end());
    mNumDeadObjects -= num_removed;

    // TODO(Beq) If this still happens, we ought to realign at this point. Do a full sweep and reset.
    if ( mNumDeadObjects != mDeadObjects.size() )
    {
        LL_WARNS_ONCE() << "Num dead objects (" << mNumDeadObjects << ") != dead object list size (" << mDeadObjects.size() << "),  deadlist discrepancy (" << num_divergent << ")" << LL_ENDL;
    }
    // </FS:Ansariel>
}

void LLViewerObjectList::removeFromActiveList(LLViewerObject* objectp)
{
    S32 idx = objectp->getListIndex();
    if (idx != -1)
    {
        objectp->setListIndex(-1);

        S32 size = (S32)mActiveObjects.size();
        if (size > 0) // mActiveObjects could have been cleaned already
        {
            // Remove by moving last element to this object's position

            llassert(idx < size); // idx should be always within mActiveObjects, unless killAllObjects was called
            llassert(mActiveObjects[idx] == objectp); // object should be there

            S32 last_index = size - 1;
            if (idx < last_index)
            {
                mActiveObjects[idx] = mActiveObjects[last_index];
                mActiveObjects[idx]->setListIndex(idx);
            } // else assume it's the last element, no need to swap
            mActiveObjects.pop_back();
        }
    }
}

void LLViewerObjectList::updateActive(LLViewerObject *objectp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWABLE;

    if (objectp->isDead())
    {
        return; // We don't update dead objects!
    }

    bool active = objectp->isActive();
    if (active != objectp->onActiveList())
    {
        if (active)
        {
            //LL_INFOS() << "Adding " << objectp->mID << " " << objectp->getPCodeString() << " to active list." << LL_ENDL;
            S32 idx = objectp->getListIndex();
            if (idx <= -1)
            {
                mActiveObjects.push_back(objectp);
                objectp->setListIndex(static_cast<S32>(mActiveObjects.size()) - 1);
            objectp->setOnActiveList(true);
            }
            else
            {
                llassert(idx < mActiveObjects.size());
                llassert(mActiveObjects[idx] == objectp);

                if (idx >= mActiveObjects.size() ||
                    mActiveObjects[idx] != objectp)
                {
                    LL_WARNS() << "Invalid object list index detected!" << LL_ENDL;
                }
            }
        }
        else
        {
            //LL_INFOS() << "Removing " << objectp->mID << " " << objectp->getPCodeString() << " from active list." << LL_ENDL;
            removeFromActiveList(objectp);
            objectp->setOnActiveList(false);
        }
    }

    //post condition: if object is active, it must be on the active list
    llassert(!active || std::find(mActiveObjects.begin(), mActiveObjects.end(), objectp) != mActiveObjects.end());

    //post condition: if object is not active, it must not be on the active list
    llassert(active || std::find(mActiveObjects.begin(), mActiveObjects.end(), objectp) == mActiveObjects.end());
}

void LLViewerObjectList::updateObjectCost(LLViewerObject* object)
{
    if (!object->isRoot())
    { //always fetch cost for the parent when fetching cost for children
        mStaleObjectCost.insert(((LLViewerObject*)object->getParent())->getID());
    }
    mStaleObjectCost.insert(object->getID());
}

void LLViewerObjectList::updateObjectCost(const LLUUID& object_id, F32 object_cost, F32 link_cost, F32 physics_cost, F32 link_physics_cost)
{
    LLViewerObject* object = findObject(object_id);
    if (object)
    {
        object->setObjectCost(object_cost);
        object->setLinksetCost(link_cost);
        object->setPhysicsCost(physics_cost);
        object->setLinksetPhysicsCost(link_physics_cost);
    }
}

void LLViewerObjectList::onObjectCostFetchFailure(const LLUUID& object_id)
{
    //LL_WARNS() << "Failed to fetch object cost for object: " << object_id << LL_ENDL;
    mPendingObjectCost.erase(object_id);
}

void LLViewerObjectList::updatePhysicsFlags(const LLViewerObject* object)
{
    mStalePhysicsFlags.insert(object->getID());
}

void LLViewerObjectList::updatePhysicsShapeType(const LLUUID& object_id, S32 type)
{
    mPendingPhysicsFlags.erase(object_id);
    LLViewerObject* object = findObject(object_id);
    if (object)
    {
        object->setPhysicsShapeType(type);
    }
}

void LLViewerObjectList::updatePhysicsProperties(const LLUUID& object_id,
                                                F32 density,
                                                F32 friction,
                                                F32 restitution,
                                                F32 gravity_multiplier)
{
    mPendingPhysicsFlags.erase(object_id);

    LLViewerObject* object = findObject(object_id);
    if (object)
    {
        object->setPhysicsDensity(density);
        object->setPhysicsFriction(friction);
        object->setPhysicsGravity(gravity_multiplier);
        object->setPhysicsRestitution(restitution);
    }
}

void LLViewerObjectList::onPhysicsFlagsFetchFailure(const LLUUID& object_id)
{
    //LL_WARNS() << "Failed to fetch physics flags for object: " << object_id << LL_ENDL;
    mPendingPhysicsFlags.erase(object_id);
}

void LLViewerObjectList::shiftObjects(const LLVector3 &offset)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    // This is called when we shift our origin when we cross region boundaries...
    // We need to update many object caches, I'll document this more as I dig through the code
    // cleaning things out...

    if (0 == offset.magVecSquared())
    {
        return;
    }


    LLViewerObject *objectp;
    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        objectp = *iter;
        // There could be dead objects on the object list, so don't update stuff if the object is dead.
        if (!objectp->isDead())
        {
            objectp->updatePositionCaches();

            if (objectp->mDrawable.notNull() && !objectp->mDrawable->isDead())
            {
                gPipeline.markShift(objectp->mDrawable);
            }
        }
    }

    gPipeline.shiftObjects(offset);

    LLWorld::getInstance()->shiftRegions(offset);
}

void LLViewerObjectList::repartitionObjects()
{
    for (vobj_list_t::iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        LLViewerObject* objectp = *iter;
        if (!objectp->isDead())
        {
            LLDrawable* drawable = objectp->mDrawable;
            if (drawable && !drawable->isDead())
            {
                drawable->updateBinRadius();
                drawable->updateSpatialExtents();
                drawable->movePartition();
            }
        }
    }
}

//debug code
bool LLViewerObjectList::hasMapObjectInRegion(LLViewerRegion* regionp)
{
    for (vobj_list_t::iterator iter = mMapObjects.begin(); iter != mMapObjects.end(); ++iter)
    {
        LLViewerObject* objectp = *iter;

        if(objectp->isDead() || objectp->getRegion() == regionp)
        {
            return true ;
        }
    }

    return false ;
}

//make sure the region is cleaned up.
void LLViewerObjectList::clearAllMapObjectsInRegion(LLViewerRegion* regionp)
{
    std::set<LLViewerObject*> dead_object_list ;
    std::set<LLViewerObject*> region_object_list ;
    for (vobj_list_t::iterator iter = mMapObjects.begin(); iter != mMapObjects.end(); ++iter)
    {
        LLViewerObject* objectp = *iter;

        if(objectp->isDead())
        {
            dead_object_list.insert(objectp) ;
        }
        else if(objectp->getRegion() == regionp)
        {
            region_object_list.insert(objectp) ;
        }
    }

    if(dead_object_list.size() > 0)
    {
        LL_WARNS() << "There are " << dead_object_list.size() << " dead objects on the map!" << LL_ENDL ;

        for(std::set<LLViewerObject*>::iterator iter = dead_object_list.begin(); iter != dead_object_list.end(); ++iter)
        {
            cleanupReferences(*iter) ;
        }
    }
    if(region_object_list.size() > 0)
    {
        LL_WARNS() << "There are " << region_object_list.size() << " objects not removed from the deleted region!" << LL_ENDL ;

        for(std::set<LLViewerObject*>::iterator iter = region_object_list.begin(); iter != region_object_list.end(); ++iter)
        {
            (*iter)->markDead() ;
        }
    }
}


void LLViewerObjectList::renderObjectsForMap(LLNetMap &netmap)
{
    static const LLUIColor above_water_color = LLUIColorTable::instance().getColor( "NetMapOtherOwnAboveWater" );
    static const LLUIColor below_water_color = LLUIColorTable::instance().getColor( "NetMapOtherOwnBelowWater" );
    static const LLUIColor you_own_above_water_color =
                        LLUIColorTable::instance().getColor( "NetMapYouOwnAboveWater" );
    static const LLUIColor you_own_below_water_color =
                        LLUIColorTable::instance().getColor( "NetMapYouOwnBelowWater" );
    static const LLUIColor group_own_above_water_color =
                        LLUIColorTable::instance().getColor( "NetMapGroupOwnAboveWater" );
    static const LLUIColor group_own_below_water_color =
                        LLUIColorTable::instance().getColor( "NetMapGroupOwnBelowWater" );

// <FS:CR> FIRE-1846: Firestorm netmap enhancements
    static const LLUIColor you_own_physical_color = LLUIColorTable::instance().getColor ( "NetMapYouPhysical", LLColor4::red );
    static const LLUIColor group_own_physical_color = LLUIColorTable::instance().getColor ( "NetMapGroupPhysical", LLColor4::green );
    static const LLUIColor other_own_physical_color = LLUIColorTable::instance().getColor ( "NetMapOtherPhysical", LLColor4::green );
    static const LLUIColor scripted_object_color = LLUIColorTable::instance().getColor ( "NetMapScripted", LLColor4::orange );
    static const LLUIColor temp_on_rez_object_color = LLUIColorTable::instance().getColor ( "NetMapTempOnRez", LLColor4::orange );
    static LLCachedControl<bool> fs_netmap_physical(gSavedSettings, "FSNetMapPhysical", false);
    static LLCachedControl<bool> fs_netmap_scripted(gSavedSettings, "FSNetMapScripted", false);
    static LLCachedControl<bool> fs_netmap_temp_on_rez(gSavedSettings, "FSNetMapTempOnRez", false);
    static LLCachedControl<U32> fs_netmap_phantom_opacity(gSavedSettings, "FSNetMapPhantomOpacity", 100);
    const F32 MIN_RADIUS_FOR_ACCENTED_OBJECTS = 2.f;
// </FS:CR>
    static LLCachedControl<F32> max_radius(gSavedSettings, "MiniMapPrimMaxRadius");
    static LLCachedControl<F32> max_zdistance_from_avatar(gSavedSettings, "MiniMapPrimMaxVertDistance");

    for (vobj_list_t::iterator iter = mMapObjects.begin(); iter != mMapObjects.end(); ++iter)
    {
        LLViewerObject* objectp = *iter;

        if(objectp->isDead())//some dead objects somehow not cleaned.
        {
            continue ;
        }

        if (!objectp->getRegion() || objectp->isOrphaned() || objectp->isAttachment())
        {
            continue;
        }
        const LLVector3& scale = objectp->getScale();
        const LLVector3d pos = objectp->getPositionGlobal();
        const F64 water_height = F64( objectp->getRegion()->getWaterHeight() );

        // Skip all objects that are more than MiniMapPrimMaxVertDistance above or below the avatar
        if (max_zdistance_from_avatar > 0.0)
        {
            F64 zdistance = pos.mdV[VZ] - gAgent.getPositionGlobal().mdV[VZ];
            if (zdistance < (-max_zdistance_from_avatar) || zdistance > max_zdistance_from_avatar)
            {
                continue;
            }
        }

        F32 approx_radius = (scale.mV[VX] + scale.mV[VY]) * 0.5f * 0.5f * 1.3f;  // 1.3 is a fudge

        // Limit the size of megaprims so they don't blot out everything on the minimap.
        // Attempting to draw very large megaprims also causes client lag.
        // See DEV-17370 and DEV-29869/SNOW-79 for details.
        approx_radius = llmin(approx_radius, (F32)max_radius);

        LLColor4U color = above_water_color.get();
        if( objectp->permYouOwner() )
        {
            const F32 MIN_RADIUS_FOR_OWNED_OBJECTS = 2.f;
            if( approx_radius < MIN_RADIUS_FOR_OWNED_OBJECTS )
            {
                approx_radius = MIN_RADIUS_FOR_OWNED_OBJECTS;
            }

            if( pos.mdV[VZ] >= water_height )
            {
                if ( objectp->permGroupOwner() )
                {
                    color = group_own_above_water_color.get();
                }
                else
                {
                color = you_own_above_water_color.get();
            }
            }
            else
            {
                if ( objectp->permGroupOwner() )
                {
                    color = group_own_below_water_color.get();
                }
            else
            {
                color = you_own_below_water_color.get();
            }
        }
        }
        else
        if( pos.mdV[VZ] < water_height )
        {
            color = below_water_color.get();
        }

// <FS:CR> FIRE-1846: Firestorm netmap enhancements
        if (fs_netmap_scripted && objectp->flagScripted())
        {
            color = scripted_object_color.get();
            if( approx_radius < MIN_RADIUS_FOR_ACCENTED_OBJECTS )
            {
                approx_radius = MIN_RADIUS_FOR_ACCENTED_OBJECTS;
            }
        }

        if (fs_netmap_physical && objectp->flagUsePhysics())
        {
            if (objectp->permYouOwner())
            {
                color = you_own_physical_color.get();
            }
            else if (objectp->permGroupOwner())
            {
                color = group_own_physical_color.get();
            }
            else
            {
                color = other_own_physical_color.get();
            }
            if( approx_radius < MIN_RADIUS_FOR_ACCENTED_OBJECTS )
            {
                approx_radius = MIN_RADIUS_FOR_ACCENTED_OBJECTS;
            }
        }

        if (fs_netmap_temp_on_rez && objectp->flagTemporaryOnRez())
        {
            color = temp_on_rez_object_color.get();
            if( approx_radius < MIN_RADIUS_FOR_ACCENTED_OBJECTS )
            {
                approx_radius = MIN_RADIUS_FOR_ACCENTED_OBJECTS;
            }
        }

        if (objectp->flagPhantom())
        {
            color.setAlpha(llclampb((U32)fs_netmap_phantom_opacity));

        }
// </FS:CR>

        netmap.renderScaledPointGlobal(
            pos,
            color,
            approx_radius );
    }
}

void LLViewerObjectList::renderObjectBounds(const LLVector3 &center)
{
}

extern bool gCubeSnapshot;

void LLViewerObjectList::addDebugBeacon(const LLVector3 &pos_agent,
                                        const std::string &string,
                                        const LLColor4 &color,
                                        const LLColor4 &text_color,
                                        S32 line_width)
{
    llassert(!gCubeSnapshot);
    LLDebugBeacon beacon;
    beacon.mPositionAgent = pos_agent;
    beacon.mString = string;
    beacon.mColor = color;
    beacon.mTextColor = text_color;
    beacon.mLineWidth = line_width;

    mDebugBeacons.push_back(beacon);
}

void LLViewerObjectList::resetObjectBeacons()
{
    mDebugBeacons.clear();
}

LLViewerObject *LLViewerObjectList::createObjectViewer(const LLPCode pcode, LLViewerRegion *regionp, S32 flags)
{
    LLUUID fullid;
    fullid.generate();

    LLViewerObject *objectp = LLViewerObject::createObject(fullid, pcode, regionp, flags);
    if (!objectp)
    {
//      LL_WARNS() << "Couldn't create object of type " << LLPrimitive::pCodeToString(pcode) << LL_ENDL;
        return NULL;
    }

    mUUIDObjectMap[fullid] = objectp;

    mObjects.push_back(objectp);

    updateActive(objectp);

    return objectp;
}

LLViewerObject *LLViewerObjectList::createObjectFromCache(const LLPCode pcode, LLViewerRegion *regionp, const LLUUID &uuid, const U32 local_id)
{
    llassert_always(uuid.notNull());

    LL_DEBUGS("ObjectUpdate") << "creating " << uuid << " local_id " << local_id << LL_ENDL;

    LLViewerObject *objectp = LLViewerObject::createObject(uuid, pcode, regionp);
    if (!objectp)
    {
//      LL_WARNS() << "Couldn't create object of type " << LLPrimitive::pCodeToString(pcode) << " id:" << fullid << LL_ENDL;
        return NULL;
    }

    objectp->mLocalID = local_id;
    mUUIDObjectMap[uuid] = objectp;
    setUUIDAndLocal(uuid,
                    local_id,
                    regionp->getHost().getAddress(),
                    regionp->getHost().getPort(),
                    objectp);
    mObjects.push_back(objectp);

    updateActive(objectp);

    return objectp;
}

LLViewerObject *LLViewerObjectList::createObject(const LLPCode pcode, LLViewerRegion *regionp,
                                                 const LLUUID &uuid, const U32 local_id, const LLHost &sender)
{
    // <FS:Ansariel> Don't create derendered objects
    if (mDerendered.end() != mDerendered.find(uuid))
    {
        return NULL;
    }
    // </FS:Ansariel>

    // <FS:Ansariel> FIRE-20288: Option to render friends only
    if (isNonFriendDerendered(uuid, pcode))
    {
        return NULL;
    }
    // </FS:Ansariel>

    LLUUID fullid;
    if (uuid == LLUUID::null)
    {
        fullid.generate();
    }
    else
    {
        fullid = uuid;
    }

    LL_DEBUGS("ObjectUpdate") << "createObject creating " << fullid << LL_ENDL;

    LLViewerObject *objectp = LLViewerObject::createObject(fullid, pcode, regionp);
    if (!objectp)
    {
//      LL_WARNS() << "Couldn't create object of type " << LLPrimitive::pCodeToString(pcode) << " id:" << fullid << LL_ENDL;
        return NULL;
    }
    if(regionp)
    {
        regionp->addToCreatedList(local_id);
    }

    mUUIDObjectMap[fullid] = objectp;
    setUUIDAndLocal(fullid,
                    local_id,
                    gMessageSystem->getSenderIP(),
                    gMessageSystem->getSenderPort(),
                    objectp);

    mObjects.push_back(objectp);

    updateActive(objectp);

    return objectp;
}

LLViewerObject *LLViewerObjectList::replaceObject(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
{
    LLViewerObject *old_instance = findObject(id);
    if (old_instance)
    {
        //cleanupReferences(old_instance);
        old_instance->markDead();

        return createObject(pcode, regionp, id, old_instance->getLocalID(), LLHost());
    }
    return NULL;
}

S32 LLViewerObjectList::findReferences(LLDrawable *drawablep) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWABLE;

    LLViewerObject *objectp;
    S32 num_refs = 0;

    for (vobj_list_t::const_iterator iter = mObjects.begin(); iter != mObjects.end(); ++iter)
    {
        objectp = *iter;
        if (objectp->mDrawable.notNull())
        {
            num_refs += objectp->mDrawable->findReferences(drawablep);
        }
    }
    return num_refs;
}

std::vector<LLUUID> LLViewerObjectList::findMeshObjectsBySculptID(LLUUID target_sculpt_id)
{
    std::vector<LLUUID> result;
    // getting IDs rather than object/vovobject pointers here because
    // of the extra safety if later calling them through findObject

    for (auto current_object : mObjects)
    {
        if ((current_object->isMesh()) &&
            (current_object->getVolume()) &&
            (current_object->getVolume()->getParams().getSculptID() == target_sculpt_id))
        {
            result.push_back(current_object->getID());
        }
    }

    return result;
}

void LLViewerObjectList::orphanize(LLViewerObject *childp, U32 parent_id, U32 ip, U32 port)
{
    LL_DEBUGS("ORPHANS") << "Orphaning object " << childp->getID() << " with parent " << parent_id << LL_ENDL;

    // We're an orphan, flag things appropriately.
    childp->mOrphaned = true;
    if (childp->mDrawable.notNull())
    {
        bool make_invisible = true;
        LLViewerObject *parentp = (LLViewerObject *)childp->getParent();
        if (parentp)
        {
            if (parentp->getRegion() != childp->getRegion())
            {
                // This is probably an object flying across a region boundary, the
                // object probably ISN'T being reparented, but just got an object
                // update out of order (child update before parent).
                make_invisible = false;
                //LL_INFOS() << "Don't make object handoffs invisible!" << LL_ENDL;
            }
        }

        if (make_invisible)
        {
            // Make sure that this object becomes invisible if it's an orphan
            childp->mDrawable->setState(LLDrawable::FORCE_INVISIBLE);
        }
    }

    // Unknown parent, add to orpaned child list
    U64 parent_info = getIndex(parent_id, ip, port);

    if (std::find(mOrphanParents.begin(), mOrphanParents.end(), parent_info) == mOrphanParents.end())
    {
        mOrphanParents.push_back(parent_info);
    }

    LLViewerObjectList::OrphanInfo oi(parent_info, childp->mID);
    if (std::find(mOrphanChildren.begin(), mOrphanChildren.end(), oi) == mOrphanChildren.end())
    {
        mOrphanChildren.push_back(oi);
        mNumOrphans++;
    }
}


void LLViewerObjectList::findOrphans(LLViewerObject* objectp, U32 ip, U32 port)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;

    if (objectp->isDead())
    {
        LL_WARNS() << "Trying to find orphans for dead obj " << objectp->mID
            << ":" << objectp->getPCodeString() << LL_ENDL;
        return;
    }

    //search object cache to get orphans
    if(objectp->getRegion())
    {
        objectp->getRegion()->findOrphans(objectp->getLocalID());
    }

    // See if we are a parent of an orphan.
    // Note:  This code is fairly inefficient but it should happen very rarely.
    // It can be sped up if this is somehow a performance issue...
    if (mOrphanParents.empty())
    {
        // no known orphan parents
        return;
    }
    if (std::find(mOrphanParents.begin(), mOrphanParents.end(), getIndex(objectp->mLocalID, ip, port)) == mOrphanParents.end())
    {
        // did not find objectp in OrphanParent list
        return;
    }

    U64 parent_info = getIndex(objectp->mLocalID, ip, port);
    bool orphans_found = false;
    // Iterate through the orphan list, and set parents of matching children.

    for (std::vector<OrphanInfo>::iterator iter = mOrphanChildren.begin(); iter != mOrphanChildren.end(); )
    {
        if (iter->mParentInfo != parent_info)
        {
            ++iter;
            continue;
        }
        LLViewerObject *childp = findObject(iter->mChildInfo);
        if (childp)
        {
            if (childp == objectp)
            {
                LL_WARNS() << objectp->mID << " has self as parent, skipping!"
                    << LL_ENDL;
                ++iter;
                continue;
            }

            LL_DEBUGS("ORPHANS") << "Reunited parent " << objectp->mID
                << " with child " << childp->mID << LL_ENDL;
            LL_DEBUGS("ORPHANS") << "Glob: " << objectp->getPositionGlobal() << LL_ENDL;
            LL_DEBUGS("ORPHANS") << "Agent: " << objectp->getPositionAgent() << LL_ENDL;
#ifdef ORPHAN_SPAM
            addDebugBeacon(objectp->getPositionAgent(),"");
#endif
            gPipeline.markMoved(objectp->mDrawable);
            objectp->setChanged(LLXform::MOVED | LLXform::SILHOUETTE);

            // Flag the object as no longer orphaned
            childp->mOrphaned = false;
            if (childp->mDrawable.notNull())
            {
                // Make the drawable visible again and set the drawable parent
                childp->mDrawable->clearState(LLDrawable::FORCE_INVISIBLE);
                childp->setDrawableParent(objectp->mDrawable); // LLViewerObjectList::findOrphans()
                gPipeline.markRebuild( childp->mDrawable, LLDrawable::REBUILD_ALL);
            }

            // Make certain particles, icon and HUD aren't hidden
            childp->hideExtraDisplayItems( false );

            objectp->addChild(childp);
            orphans_found = true;
            ++iter;
        }
        else
        {
            // <FS:Beq> descope uninteresting spam we can do nothing about.
            // LL_INFOS() << "Missing orphan child, removing from list" << LL_ENDL;
            LL_DEBUGS() << "Missing orphan child, removing from list" << LL_ENDL;

            iter = mOrphanChildren.erase(iter);
        }
    }

    // Remove orphan parent and children from lists now that they've been found
    {
        std::vector<U64>::iterator iter = std::find(mOrphanParents.begin(), mOrphanParents.end(), parent_info);
        if (iter != mOrphanParents.end())
        {
            mOrphanParents.erase(iter);
        }
    }

    for (std::vector<OrphanInfo>::iterator iter = mOrphanChildren.begin(); iter != mOrphanChildren.end(); )
    {
        if (iter->mParentInfo == parent_info)
        {
            iter = mOrphanChildren.erase(iter);
            mNumOrphans--;
        }
        else
        {
            ++iter;
        }
    }

    if (orphans_found && objectp->isSelected())
    {
        LLSelectNode* nodep = LLSelectMgr::getInstance()->getSelection()->findNode(objectp);
        if (nodep && !nodep->mIndividualSelection)
        {
            // rebuild selection with orphans
            LLSelectMgr::getInstance()->deselectObjectAndFamily(objectp);
            LLSelectMgr::getInstance()->selectObjectAndFamily(objectp);
        }
    }
}

////////////////////////////////////////////////////////////////////////////

LLViewerObjectList::OrphanInfo::OrphanInfo()
    : mParentInfo(0)
{
}

LLViewerObjectList::OrphanInfo::OrphanInfo(const U64 parent_info, const LLUUID child_info)
    : mParentInfo(parent_info), mChildInfo(child_info)
{
}

bool LLViewerObjectList::OrphanInfo::operator==(const OrphanInfo &rhs) const
{
    return (mParentInfo == rhs.mParentInfo) && (mChildInfo == rhs.mChildInfo);
}

bool LLViewerObjectList::OrphanInfo::operator!=(const OrphanInfo &rhs) const
{
    return !operator==(rhs);
}


LLDebugBeacon::~LLDebugBeacon()
{
    if (mHUDObject.notNull())
    {
        mHUDObject->markDead();
    }
}

// <FS:ND> Helper function to purge the internal list of derendered objects on teleport.
void LLViewerObjectList::resetDerenderList(bool force /*= false*/)
{
    static LLCachedControl<bool> fsTempDerenderUntilTeleport(gSavedSettings, "FSTempDerenderUntilTeleport");
    if (!fsTempDerenderUntilTeleport && !force)
    {
        return;
    }

    std::map< LLUUID, bool > oDerendered;
    uuid_vec_t removed_ids;

    for (std::map< LLUUID, bool >::iterator itr = mDerendered.begin(); itr != mDerendered.end(); ++itr)
    {
        if (itr->second)
        {
            oDerendered[itr->first] = itr->second;
        }
        else
        {
            removed_ids.push_back(itr->first);
        }
    }

    mDerendered.swap( oDerendered );
    FSAssetBlacklist::instance().removeItemsFromBlacklist(removed_ids);
}

// <FS:ND> Helper function to add items from global blacklist after teleport.
void LLViewerObjectList::addDerenderedItem( LLUUID const &aId, bool aPermanent )
{
    mDerendered[ aId ] = aPermanent;
}
void LLViewerObjectList::removeDerenderedItem( LLUUID const &aId )
{
    mDerendered.erase( aId );
}
// </FS:ND>

// <FS:Ansariel> FIRE-20288: Option to render friends only
bool LLViewerObjectList::isNonFriendDerendered(const LLUUID& id, LLPCode pcode)
{
    static LLCachedControl<bool> fsRenderFriendsOnly(gSavedPerAccountSettings, "FSRenderFriendsOnly");
    return (pcode == LL_PCODE_LEGACY_AVATAR && fsRenderFriendsOnly && id != gAgentID && !LLAvatarActions::isFriend(id));
}
// </FS:Ansariel>
