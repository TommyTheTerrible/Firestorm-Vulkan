
/**
 * @file llviewertexture.cpp
 * @brief Object which handles a received image (and associated texture(s))
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#include "llviewertexture.h"

// Library includes
#include "llmath.h"
#include "llerror.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llhost.h"
#include "llimage.h"
#include "llimagebmp.h"
#include "llimagej2c.h"
#include "llimagetga.h"
#include "llstl.h"
#include "message.h"
#include "lltimer.h"
#include "v4coloru.h"
#include "llnotificationsutil.h"

// viewer includes
#include "llimagegl.h"
#include "lldrawpool.h"
#include "lltexturefetch.h"
#include "llviewertexturelist.h"
#include "llviewercontrol.h"
#include "pipeline.h"
#include "llappviewer.h"
#include "llface.h"
#include "llviewercamera.h"
#include "lltextureentry.h"
#include "lltexturemanagerbridge.h"
#include "llmediaentry.h"
#include "llvovolume.h"
#include "llviewermedia.h"
#include "lltexturecache.h"
#include "llviewerwindow.h"
#include "llwindow.h"
///////////////////////////////////////////////////////////////////////////////

#include "llmimetypes.h"

#include "llviewerdisplay.h"
// extern
const S32Megabytes gMinVideoRam(32);
// <FS:Ansariel> Texture memory management
//const S32Megabytes gMaxVideoRam(512);
S32Megabytes gMaxVideoRam(512);
// </FS:Ansariel>


// statics
LLPointer<LLViewerTexture>        LLViewerTexture::sNullImagep = nullptr;
LLPointer<LLViewerTexture>        LLViewerTexture::sBlackImagep = nullptr;
LLPointer<LLViewerTexture>        LLViewerTexture::sCheckerBoardImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sMissingAssetImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sWhiteImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sInvisibleImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sDefaultParticleImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sDefaultImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sSmokeImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sFlatNormalImagep = nullptr;
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sDefaultIrradiancePBRp;
// [SL:KB] - Patch: Render-TextureToggle (Catznip-4.0)
LLPointer<LLViewerFetchedTexture> LLViewerFetchedTexture::sDefaultDiffuseImagep = nullptr;
// [/SL:KB]
LLViewerMediaTexture::media_map_t LLViewerMediaTexture::sMediaMap;
LLTexturePipelineTester* LLViewerTextureManager::sTesterp = nullptr;
F32 LLViewerFetchedTexture::sMaxVirtualSize = 8192.f*8192.f;

const std::string sTesterName("TextureTester");

S32 LLViewerTexture::sImageCount = 0;
S32 LLViewerTexture::sRawCount = 0;
S32 LLViewerTexture::sAuxCount = 0;
LLFrameTimer LLViewerTexture::sEvaluationTimer;
F32 LLViewerTexture::sDesiredDiscardBias = 0.f;
U32 LLViewerTexture::sBiasTexturesUpdated = 0;

S32 LLViewerTexture::sMaxSculptRez = 128; //max sculpt image size
constexpr S32 MAX_CACHED_RAW_IMAGE_AREA = 64 * 64;
const S32 MAX_CACHED_RAW_SCULPT_IMAGE_AREA = LLViewerTexture::sMaxSculptRez * LLViewerTexture::sMaxSculptRez;
constexpr S32 MAX_CACHED_RAW_TERRAIN_IMAGE_AREA = 128 * 128;
constexpr S32 DEFAULT_ICON_DIMENSIONS = 32;
constexpr S32 DEFAULT_THUMBNAIL_DIMENSIONS = 256;
U32 LLViewerTexture::sMinLargeImageSize = 65536; //256 * 256.
U32 LLViewerTexture::sMaxSmallImageSize = MAX_CACHED_RAW_IMAGE_AREA;
bool LLViewerTexture::sFreezeImageUpdates = false;
F32 LLViewerTexture::sCurrentTime = 0.0f;

constexpr F32 MEMORY_CHECK_WAIT_TIME = 1.0f;
//constexpr F32 MIN_VRAM_BUDGET = 768.f; // <FS:Ansariel> Expose max texture VRAM setting
F32 LLViewerTexture::sFreeVRAMMegabytes = MIN_VRAM_BUDGET;

LLViewerTexture::EDebugTexels LLViewerTexture::sDebugTexelsMode = LLViewerTexture::DEBUG_TEXELS_OFF;

const F64 log_2 = log(2.0);

LLUUID LLViewerTexture::sInvisiprimTexture1 = LLUUID::null;
LLUUID LLViewerTexture::sInvisiprimTexture2 = LLUUID::null;
#define TEX_INVISIPRIM1 "e97cf410-8e61-7005-ec06-629eba4cd1fb"
#define TEX_INVISIPRIM2 "38b86f85-2575-52a9-a531-23108d8da837"

F32 nearest_power_of_two(F32 input)
{
    // https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2Float
    unsigned int const v = (unsigned int)input;
    unsigned int r;
    if (v > 1)
    {
        float f = (float) v;
        unsigned int const t = 1U << ((*(unsigned int*) &f >> 23) - 0x7f);
        unsigned int adjust = (t < v);
        r = t << adjust;
    }
    else
    {
        r = 1;
    }
    return (F32)r;
}

//----------------------------------------------------------------------------------------------
//namespace: LLViewerTextureAccess
//----------------------------------------------------------------------------------------------

LLLoadedCallbackEntry::LLLoadedCallbackEntry(loaded_callback_func cb,
                      S32 discard_level,
                      bool need_imageraw, // Needs image raw for the callback
                      void* userdata,
                      LLLoadedCallbackEntry::source_callback_list_t* src_callback_list,
                      LLViewerFetchedTexture* target,
                      bool pause)
    : mCallback(cb),
      mLastUsedDiscard(MAX_DISCARD_LEVEL+1),
      mDesiredDiscard(discard_level),
      mNeedsImageRaw(need_imageraw),
      mUserData(userdata),
      mSourceCallbackList(src_callback_list),
      mPaused(pause)
{
    if(mSourceCallbackList)
    {
        mSourceCallbackList->insert(LLTextureKey(target->getID(), (ETexListType)target->getTextureListType()));
    }
}

LLLoadedCallbackEntry::~LLLoadedCallbackEntry()
{
}

void LLLoadedCallbackEntry::removeTexture(LLViewerFetchedTexture* tex)
{
    if (mSourceCallbackList && tex)
    {
        mSourceCallbackList->erase(LLTextureKey(tex->getID(), (ETexListType)tex->getTextureListType()));
    }
}

//static
void LLLoadedCallbackEntry::cleanUpCallbackList(LLLoadedCallbackEntry::source_callback_list_t* callback_list)
{
    //clear texture callbacks.
    if(callback_list && !callback_list->empty())
    {
        for(LLLoadedCallbackEntry::source_callback_list_t::iterator iter = callback_list->begin();
                iter != callback_list->end(); ++iter)
        {
            LLViewerFetchedTexture* tex = gTextureList.findImage(*iter);
            if(tex)
            {
                tex->deleteCallbackEntry(callback_list);
            }
        }
        callback_list->clear();
    }
}

LLViewerMediaTexture* LLViewerTextureManager::createMediaTexture(const LLUUID &media_id, bool usemipmaps, LLImageGL* gl_image)
{
    return new LLViewerMediaTexture(media_id, usemipmaps, gl_image);
}

void LLViewerTextureManager::findFetchedTextures(const LLUUID& id, std::vector<LLViewerFetchedTexture*> &output)
{
    return gTextureList.findTexturesByID(id, output);
}

void  LLViewerTextureManager::findTextures(const LLUUID& id, std::vector<LLViewerTexture*> &output)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    std::vector<LLViewerFetchedTexture*> fetched_output;
    gTextureList.findTexturesByID(id, fetched_output);
    std::vector<LLViewerFetchedTexture*>::iterator iter = fetched_output.begin();
    while (iter != fetched_output.end())
    {
        output.push_back(*iter);
        iter++;
    }

    //search media texture list
    if (output.empty())
    {
        LLViewerTexture* tex;
        tex = LLViewerTextureManager::findMediaTexture(id);
        if (tex)
        {
            output.push_back(tex);
        }
    }

}

LLViewerFetchedTexture* LLViewerTextureManager::findFetchedTexture(const LLUUID& id, S32 tex_type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    return gTextureList.findImage(id, (ETexListType)tex_type);
}

LLViewerMediaTexture* LLViewerTextureManager::findMediaTexture(const LLUUID &media_id)
{
    return LLViewerMediaTexture::findMediaTexture(media_id);
}

LLViewerMediaTexture*  LLViewerTextureManager::getMediaTexture(const LLUUID& id, bool usemipmaps, LLImageGL* gl_image)
{
    LLViewerMediaTexture* tex = LLViewerMediaTexture::findMediaTexture(id);
    if(!tex)
    {
        tex = LLViewerTextureManager::createMediaTexture(id, usemipmaps, gl_image);
    }

    tex->initVirtualSize();

    return tex;
}

LLViewerFetchedTexture* LLViewerTextureManager::staticCastToFetchedTexture(LLTexture* tex, bool report_error)
{
    if(!tex)
    {
        return NULL;
    }

    S8 type = tex->getType();
    if(type == LLViewerTexture::FETCHED_TEXTURE || type == LLViewerTexture::LOD_TEXTURE)
    {
        return static_cast<LLViewerFetchedTexture*>(tex);
    }

    if(report_error)
    {
        LL_ERRS() << "not a fetched texture type: " << type << LL_ENDL;
    }

    return NULL;
}

LLPointer<LLViewerTexture> LLViewerTextureManager::getLocalTexture(bool usemipmaps, bool generate_gl_tex)
{
    LLPointer<LLViewerTexture> tex = new LLViewerTexture(usemipmaps);
    if(generate_gl_tex)
    {
        tex->generateGLTexture();
        tex->setCategory(LLGLTexture::LOCAL);
    }
    return tex;
}
LLPointer<LLViewerTexture> LLViewerTextureManager::getLocalTexture(const LLUUID& id, bool usemipmaps, bool generate_gl_tex)
{
    LLPointer<LLViewerTexture> tex = new LLViewerTexture(id, usemipmaps);
    if(generate_gl_tex)
    {
        tex->generateGLTexture();
        tex->setCategory(LLGLTexture::LOCAL);
    }
    return tex;
}
LLPointer<LLViewerTexture> LLViewerTextureManager::getLocalTexture(const LLImageRaw* raw, bool usemipmaps)
{
    LLPointer<LLViewerTexture> tex = new LLViewerTexture(raw, usemipmaps);
    tex->setCategory(LLGLTexture::LOCAL);
    return tex;
}
LLPointer<LLViewerTexture> LLViewerTextureManager::getLocalTexture(const U32 width, const U32 height, const U8 components, bool usemipmaps, bool generate_gl_tex)
{
    LLPointer<LLViewerTexture> tex = new LLViewerTexture(width, height, components, usemipmaps);
    if(generate_gl_tex)
    {
        tex->generateGLTexture();
        tex->setCategory(LLGLTexture::LOCAL);
    }
    return tex;
}

LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTexture(const LLImageRaw* raw, FTType type, bool usemipmaps)
{
    LLImageDataSharedLock lock(raw);
    LLViewerFetchedTexture* ret = new LLViewerFetchedTexture(raw, type, usemipmaps);
    gTextureList.addImage(ret, TEX_LIST_STANDARD);
    return ret;
}

LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTexture(
                                                   const LLUUID &image_id,
                                                   FTType f_type,
                                                   bool usemipmaps,
                                                   LLViewerTexture::EBoostLevel boost_priority,
                                                   S8 texture_type,
                                                   LLGLint internal_format,
                                                   LLGLenum primary_format,
                                                   LLHost request_from_host)
{
    return gTextureList.getImage(image_id, f_type, usemipmaps, boost_priority, texture_type, internal_format, primary_format, request_from_host);
}

LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromFile(
                                                   const std::string& filename,
                                                   FTType f_type,
                                                   bool usemipmaps,
                                                   LLViewerTexture::EBoostLevel boost_priority,
                                                   S8 texture_type,
                                                   LLGLint internal_format,
                                                   LLGLenum primary_format,
                                                   const LLUUID& force_id)
{
    return gTextureList.getImageFromFile(filename, f_type, usemipmaps, boost_priority, texture_type, internal_format, primary_format, force_id);
}

//static
LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromUrl(const std::string& url,
                                     FTType f_type,
                                     bool usemipmaps,
                                     LLViewerTexture::EBoostLevel boost_priority,
                                     S8 texture_type,
                                     LLGLint internal_format,
                                     LLGLenum primary_format,
                                     const LLUUID& force_id
                                     )
{
    return gTextureList.getImageFromUrl(url, f_type, usemipmaps, boost_priority, texture_type, internal_format, primary_format, force_id);
}

//static
LLImageRaw* LLViewerTextureManager::getRawImageFromMemory(const U8* data, U32 size, std::string_view mimetype)
{
    return gTextureList.getRawImageFromMemory(data, size, mimetype);
}

//static
LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromMemory(const U8* data, U32 size, std::string_view mimetype)
{
    return gTextureList.getImageFromMemory(data, size, mimetype);
}

LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromHost(const LLUUID& image_id, FTType f_type, LLHost host)
{
    return gTextureList.getImageFromHost(image_id, f_type, host);
}

// Create a bridge to the viewer texture manager.
class LLViewerTextureManagerBridge : public LLTextureManagerBridge
{
    /*virtual*/ LLPointer<LLGLTexture> getLocalTexture(bool usemipmaps = true, bool generate_gl_tex = true)
    {
        return LLViewerTextureManager::getLocalTexture(usemipmaps, generate_gl_tex);
    }

    /*virtual*/ LLPointer<LLGLTexture> getLocalTexture(const U32 width, const U32 height, const U8 components, bool usemipmaps, bool generate_gl_tex = true)
    {
        return LLViewerTextureManager::getLocalTexture(width, height, components, usemipmaps, generate_gl_tex);
    }

    /*virtual*/ LLGLTexture* getFetchedTexture(const LLUUID &image_id)
    {
        return LLViewerTextureManager::getFetchedTexture(image_id);
    }
};


void LLViewerTextureManager::init()
{
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(1,1,3);
        raw->clear(0x77, 0x77, 0x77, 0xFF);
        LLViewerTexture::sNullImagep = LLViewerTextureManager::getLocalTexture(raw.get(), true);
    }

    const S32 dim = 128;
    LLPointer<LLImageRaw> image_raw = new LLImageRaw(dim,dim,3);
    U8* data = image_raw->getData();

    memset(data, 0, dim * dim * 3);
    LLViewerTexture::sBlackImagep = LLViewerTextureManager::getLocalTexture(image_raw.get(), true);

#if 1
    LLPointer<LLViewerFetchedTexture> imagep = LLViewerTextureManager::getFetchedTexture(IMG_DEFAULT);
    LLViewerFetchedTexture::sDefaultImagep = imagep;

    for (S32 i = 0; i<dim; i++)
    {
        for (S32 j = 0; j<dim; j++)
        {
#if 0
            const S32 border = 2;
            if (i<border || j<border || i>=(dim-border) || j>=(dim-border))
            {
                *data++ = 0xff;
                *data++ = 0xff;
                *data++ = 0xff;
            }
            else
#endif
            {
                *data++ = 0x7f;
                *data++ = 0x7f;
                *data++ = 0x7f;
            }
        }
    }
    imagep->createGLTexture(0, image_raw);
    image_raw = NULL;
#else
    LLViewerFetchedTexture::sDefaultImagep = LLViewerTextureManager::getFetchedTexture(IMG_DEFAULT, true, LLGLTexture::BOOST_UI);
#endif
    LLViewerFetchedTexture::sDefaultImagep->dontDiscard();
    LLViewerFetchedTexture::sDefaultImagep->setCategory(LLGLTexture::OTHER);

    image_raw = new LLImageRaw(32,32,3);
    data = image_raw->getData();

    for (S32 i = 0; i < (32*32*3); i+=3)
    {
        S32 x = (i % (32*3)) / (3*16);
        S32 y = i / (32*3*16);
        U8 color = ((x + y) % 2) * 255;
        data[i] = color;
        data[i+1] = color;
        data[i+2] = color;
    }

    LLViewerTexture::sCheckerBoardImagep = LLViewerTextureManager::getLocalTexture(image_raw.get(), true);

    LLViewerTexture::initClass();

    // Create a texture manager bridge.
    gTextureManagerBridgep = new LLViewerTextureManagerBridge;

    if (LLMetricPerformanceTesterBasic::isMetricLogRequested(sTesterName) && !LLMetricPerformanceTesterBasic::getTester(sTesterName))
    {
        sTesterp = new LLTexturePipelineTester();
        if (!sTesterp->isValid())
        {
            delete sTesterp;
            sTesterp = NULL;
        }
    }
}

void LLViewerTextureManager::cleanup()
{
    stop_glerror();

    delete gTextureManagerBridgep;
    LLImageGL::sDefaultGLTexture = NULL;
    LLViewerTexture::sNullImagep = NULL;
    LLViewerTexture::sBlackImagep = NULL;
    LLViewerTexture::sCheckerBoardImagep = NULL;
    LLViewerFetchedTexture::sDefaultImagep = NULL;
    LLViewerFetchedTexture::sSmokeImagep = NULL;
    LLViewerFetchedTexture::sMissingAssetImagep = NULL;
    LLTexUnit::sWhiteTexture = 0;
    LLViewerFetchedTexture::sWhiteImagep = NULL;
    LLViewerFetchedTexture::sInvisibleImagep = NULL;
    LLViewerFetchedTexture::sFlatNormalImagep = NULL;
    LLViewerFetchedTexture::sDefaultIrradiancePBRp = NULL;

    LLViewerMediaTexture::cleanUpClass();
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//start of LLViewerTexture
//----------------------------------------------------------------------------------------------
// static
void LLViewerTexture::initClass()
{
    LLImageGL::sDefaultGLTexture = LLViewerFetchedTexture::sDefaultImagep->getGLTexture();

    if (sInvisiprimTexture1.isNull())
    {
        sInvisiprimTexture1 = LLUUID(TEX_INVISIPRIM1);
    }
    if (sInvisiprimTexture2.isNull())
    {
        sInvisiprimTexture2 = LLUUID(TEX_INVISIPRIM2);
    }
}

//static
void LLViewerTexture::updateClass()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    sCurrentTime = gFrameTimeSeconds;

    LLTexturePipelineTester* tester = (LLTexturePipelineTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
    if (tester)
    {
        tester->update();
    }

    LLViewerMediaTexture::updateClass();

    //if (LLAppViewer::instance()->getTextureFetch()->getNumRequests() == 0)
        //gTextureList.mListMemoryIncomingBytes = 0;

    static LLCachedControl<U32> max_vram_budget(gSavedSettings, "RenderMaxVRAMBudget", 0);
    static LLCachedControl<bool> max_vram_budget_enabled(gSavedSettings, "FSLimitTextureVRAMUsage"); // <FS:Ansariel> Expose max texture VRAM setting

    F64 texture_bytes_alloc = LLImageGL::getTextureBytesAllocated() / 1024.0 / 1024.0 * 1.3333f;
    F64 vertex_bytes_alloc = LLVertexBuffer::getBytesAllocated() / 1024.0 / 1024.0;
    F64 render_bytes_alloc = LLRenderTarget::sBytesAllocated / 1024.0 / 1024.0;

    // get an estimate of how much video memory we're using
    // NOTE: our metrics miss about half the vram we use, so this biases high but turns out to typically be within 5% of the real number
    F32 used = (F32)ll_round(texture_bytes_alloc + vertex_bytes_alloc);

    // <FS:Ansariel> Expose max texture VRAM setting
    //F32 budget = max_vram_budget == 0 ? (F32)gGLManager.mVRAM : (F32)max_vram_budget;
    F32 budget = (max_vram_budget_enabled && max_vram_budget > 0) ? (F32)max_vram_budget : (F32) gGLManager.mVRAM;

    // TommyTheTerrible - Start Bias creep upwards at 4/5ths VRAM used.
    F32 target         = llmax((budget * 0.20f), MIN_VRAM_BUDGET);
    sDesiredDiscardBias = llmax(used / target, 1.f);

    sFreeVRAMMegabytes  = llmax(budget - used, 0.f);


    LLViewerTexture::sFreezeImageUpdates = false; // sDesiredDiscardBias > (desired_discard_bias_max - 1.0f);
    //static LLCachedControl<F32> draw_distance(gSavedSettings, "RenderFarClip");
    if (LLViewerTexture::sDesiredDiscardBias < 5)
    {
        // Reduce draw distance every 2 seconds above 5 but make sure desired draw distance is saved.
    }
}

//end of static functions
//-------------------------------------------------------------------------------------------
const U32 LLViewerTexture::sCurrentFileVersion = 1;

LLViewerTexture::LLViewerTexture(bool usemipmaps) :
    LLGLTexture(usemipmaps)
{
    init(true);

    mID.generate();
    sImageCount++;
}

LLViewerTexture::LLViewerTexture(const LLUUID& id, bool usemipmaps) :
    LLGLTexture(usemipmaps),
    mID(id)
{
    init(true);

    sImageCount++;
}

LLViewerTexture::LLViewerTexture(const U32 width, const U32 height, const U8 components, bool usemipmaps)  :
    LLGLTexture(width, height, components, usemipmaps)
{
    init(true);

    mID.generate();
    sImageCount++;
}

LLViewerTexture::LLViewerTexture(const LLImageRaw* raw, bool usemipmaps) :
    LLGLTexture(raw, usemipmaps)
{
    init(true);

    mID.generate();
    sImageCount++;
}

LLViewerTexture::~LLViewerTexture()
{
    // LL_DEBUGS("Avatar") << mID << LL_ENDL;
    cleanup();
    sImageCount--;
}

// virtual
void LLViewerTexture::init(bool firstinit)
{
    mMaxVirtualSize = 0.f;
    mMaxVirtualSizeResetInterval = 1;
    mMaxVirtualSizeResetCounter = mMaxVirtualSizeResetInterval;
    mParcelMedia = NULL;

    memset(&mNumVolumes, 0, sizeof(U32)* LLRender::NUM_VOLUME_TEXTURE_CHANNELS);
    mVolumeList[LLRender::LIGHT_TEX].clear();
    mVolumeList[LLRender::SCULPT_TEX].clear();
    for (U32 i = 0; i < LLRender::NUM_TEXTURE_CHANNELS; i++)
    {
        mNumFaces[i] = 0;
        mFaceList[i].clear();
    }

    mMainQueue  = LL::WorkQueue::getInstance("mainloop");
    mImageQueue = LL::WorkQueue::getInstance("LLImageGL");

    mBoostLoaded = 0;
}

//virtual
S8 LLViewerTexture::getType() const
{
    return LLViewerTexture::LOCAL_TEXTURE;
}

void LLViewerTexture::cleanup()
{
    if (LLAppViewer::getTextureFetch())
    {
        LLAppViewer::getTextureFetch()->updateRequestPriority(mID, 0.f);
    }

    for (U32 i = 0; i < LLRender::NUM_TEXTURE_CHANNELS; i++)
    {
        mNumFaces[i] = 0;
        mFaceList[i].clear();
    }
    mVolumeList[LLRender::LIGHT_TEX].clear();
    mVolumeList[LLRender::SCULPT_TEX].clear();
}

// virtual
void LLViewerTexture::dump()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLGLTexture::dump();

    LL_INFOS() << "LLViewerTexture"
            << " mID " << mID
            << LL_ENDL;
}

void LLViewerTexture::setBoostLevel(S32 level)
{
    if(mBoostLevel != level)
    {
        mBoostLevel = level;
        if(mBoostLevel != LLViewerTexture::BOOST_NONE &&
            mBoostLevel != LLViewerTexture::BOOST_SELECTED &&
            mBoostLevel != LLViewerTexture::BOOST_AVATAR_BAKED &&
            // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings, not happening with SL Viewer
            // Added the new boost levels
            mBoostLevel != LLViewerTexture::BOOST_GRASS &&
            mBoostLevel != LLViewerTexture::BOOST_LIGHT &&
            mBoostLevel != LLViewerTexture::BOOST_TREE &&
            // </FS:minerjr> [FIRE-35081]
            mBoostLevel != LLViewerTexture::BOOST_ICON &&
            mBoostLevel != LLViewerTexture::BOOST_THUMBNAIL)
        {
            setNoDelete();
            // TommyTheTerrible - What? All textures start NoDelete. Why are we doing this?
        }
    }

    // strongly encourage anything boosted to load at full res
    if (mBoostLevel > LLViewerTexture::BOOST_HIGH)
    {
        mMaxVirtualSize = MAX_IMAGE_AREA;
        /* <3T:TommyTheTerrible> This makes no sense. It's increasing the virtual size by a MIP? BOOST are always max.
        // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings, not happening with SL Viewer
        // Add additional for the important to camera and in frustum
        static LLCachedControl<F32> texture_camera_boost(gSavedSettings, "TextureCameraBoost", 7.f);
        mMaxVirtualSize = mMaxVirtualSize + (mMaxVirtualSize * 1.0f * texture_camera_boost);
        // Apply second boost based upon if the texture is close to the camera (< 16.1 meters * draw distance multiplier)
        mMaxVirtualSize = mMaxVirtualSize + (mMaxVirtualSize * 1.0f * texture_camera_boost);
        // </FS:minerjr> [FIRE-35081]
        <3T:TommyTheTerrible> */
    }
}

bool LLViewerTexture::isActiveFetching()
{
    return false;
}

bool LLViewerTexture::bindDebugImage(const S32 stage)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (stage < 0) return false;

    bool res = true;
    if (LLViewerTexture::sCheckerBoardImagep.notNull() && (this != LLViewerTexture::sCheckerBoardImagep.get()))
    {
        res = gGL.getTexUnit(stage)->bind(LLViewerTexture::sCheckerBoardImagep);
    }

    if(!res)
    {
        return bindDefaultImage(stage);
    }

    return res;
}

bool LLViewerTexture::bindDefaultImage(S32 stage)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (stage < 0) return false;

    bool res = true;
    if (LLViewerFetchedTexture::sDefaultImagep.notNull() && (this != LLViewerFetchedTexture::sDefaultImagep.get()))
    {
        // use default if we've got it
        res = gGL.getTexUnit(stage)->bind(LLViewerFetchedTexture::sDefaultImagep);
    }
    if (!res && LLViewerTexture::sNullImagep.notNull() && (this != LLViewerTexture::sNullImagep))
    {
        res = gGL.getTexUnit(stage)->bind(LLViewerTexture::sNullImagep);
    }
    if (!res)
    {
        LL_WARNS() << "LLViewerTexture::bindDefaultImage failed." << LL_ENDL;
    }
    stop_glerror();

    LLTexturePipelineTester* tester = (LLTexturePipelineTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
    if (tester)
    {
        tester->updateGrayTextureBinding();
    }
    return res;
}

//virtual
bool LLViewerTexture::isMissingAsset()const
{
    return false;
}

//virtual
void LLViewerTexture::forceImmediateUpdate()
{
}

bool LLViewerTexture::addTextureStats(F32 virtual_size) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // Adjust virtual_size to nearest power of two
    virtual_size = nearest_power_of_two(virtual_size);
    virtual_size = llmin(virtual_size, LLViewerFetchedTexture::sMaxVirtualSize);
    virtual_size = virtual_size * (virtual_size >= 2); // Nearest power of 2 to 0 is 1, so we need to catch it.
    virtual_size += (mBoostLevel == LLGLTexture::BOOST_AVATAR_BAKED); // Adding 1 if baked texture so never 0 and always loads (so avatar clouding finishes)
    bool needs_update = (mMaxVirtualSize != virtual_size);
    mNeedsGLTexture = true;
    mMaxVirtualSize = virtual_size;
    return needs_update;
}

void LLViewerTexture::resetTextureStats()
{
    mMaxVirtualSize = 0.0f;
    mMaxVirtualSizeResetCounter = 0;
}

//virtual
F32 LLViewerTexture::getMaxVirtualSize()
{
    return mMaxVirtualSize;
}

//virtual
void LLViewerTexture::setKnownDrawSize(S32 width, S32 height)
{
    //nothing here.
}

//virtual
void LLViewerTexture::addFace(U32 ch, LLFace* facep)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    llassert(ch < LLRender::NUM_TEXTURE_CHANNELS);

    if(mNumFaces[ch] >= mFaceList[ch].size())
    {
        mFaceList[ch].resize(2 * mNumFaces[ch] + 1);
    }
    mFaceList[ch][mNumFaces[ch]] = facep;
    facep->setIndexInTex(ch, mNumFaces[ch]);
    mNumFaces[ch]++;
    mLastFaceListUpdateTimer.reset();
}

//virtual
void LLViewerTexture::removeFace(U32 ch, LLFace* facep)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    llassert(ch < LLRender::NUM_TEXTURE_CHANNELS);

    if(mNumFaces[ch] > 1)
    {
        S32 index = facep->getIndexInTex(ch);
        llassert(index < (S32)mFaceList[ch].size());
        llassert(index < (S32)mNumFaces[ch]);
        mFaceList[ch][index] = mFaceList[ch][--mNumFaces[ch]];
        mFaceList[ch][index]->setIndexInTex(ch, index);
    }
    else
    {
        mFaceList[ch].clear();
        mNumFaces[ch] = 0;
    }
    mLastFaceListUpdateTimer.reset();
}

S32 LLViewerTexture::getTotalNumFaces() const
{
    S32 ret = 0;

    for (U32 i = 0; i < LLRender::NUM_TEXTURE_CHANNELS; ++i)
    {
        ret += mNumFaces[i];
    }

    return ret;
}

S32 LLViewerTexture::getNumFaces(U32 ch) const
{
    llassert(ch < LLRender::NUM_TEXTURE_CHANNELS);
    return ch < LLRender::NUM_TEXTURE_CHANNELS ? mNumFaces[ch] : 0;
}


//virtual
void LLViewerTexture::addVolume(U32 ch, LLVOVolume* volumep)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mNumVolumes[ch] >= mVolumeList[ch].size())
    {
        mVolumeList[ch].resize(2 * mNumVolumes[ch] + 1);
    }
    mVolumeList[ch][mNumVolumes[ch]] = volumep;
    volumep->setIndexInTex(ch, mNumVolumes[ch]);
    mNumVolumes[ch]++;
    mLastVolumeListUpdateTimer.reset();
}

//virtual
void LLViewerTexture::removeVolume(U32 ch, LLVOVolume* volumep)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mNumVolumes[ch] > 1)
    {
        S32 index = volumep->getIndexInTex(ch);
        llassert(index < (S32)mVolumeList[ch].size());
        llassert(index < (S32)mNumVolumes[ch]);
        mVolumeList[ch][index] = mVolumeList[ch][--mNumVolumes[ch]];
        mVolumeList[ch][index]->setIndexInTex(ch, index);
    }
    else
    {
        mVolumeList[ch].clear();
        mNumVolumes[ch] = 0;
    }
    mLastVolumeListUpdateTimer.reset();
}

S32 LLViewerTexture::getNumVolumes(U32 ch) const
{
    return mNumVolumes[ch];
}

void LLViewerTexture::reorganizeFaceList()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static const F32 MAX_WAIT_TIME = 20.f; // seconds
    static const U32 MAX_EXTRA_BUFFER_SIZE = 4;

    if(mLastFaceListUpdateTimer.getElapsedTimeF32() < MAX_WAIT_TIME)
    {
        return;
    }

    for (U32 i = 0; i < LLRender::NUM_TEXTURE_CHANNELS; ++i)
    {
        if(mNumFaces[i] + MAX_EXTRA_BUFFER_SIZE > mFaceList[i].size())
    {
        return;
    }

        mFaceList[i].erase(mFaceList[i].begin() + mNumFaces[i], mFaceList[i].end());
    }

    mLastFaceListUpdateTimer.reset();
}

void LLViewerTexture::reorganizeVolumeList()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static const F32 MAX_WAIT_TIME = 20.f; // seconds
    static const U32 MAX_EXTRA_BUFFER_SIZE = 4;


    for (U32 i = 0; i < LLRender::NUM_VOLUME_TEXTURE_CHANNELS; ++i)
    {
        if (mNumVolumes[i] + MAX_EXTRA_BUFFER_SIZE > mVolumeList[i].size())
        {
            return;
        }
    }

    if(mLastVolumeListUpdateTimer.getElapsedTimeF32() < MAX_WAIT_TIME)
    {
        return;
    }

    mLastVolumeListUpdateTimer.reset();
    for (U32 i = 0; i < LLRender::NUM_VOLUME_TEXTURE_CHANNELS; ++i)
    {
        mVolumeList[i].erase(mVolumeList[i].begin() + mNumVolumes[i], mVolumeList[i].end());
    }
}

bool LLViewerTexture::isLargeImage()
{
    return  (S32)mTexelsPerImage > LLViewerTexture::sMinLargeImageSize;
}

bool LLViewerTexture::isInvisiprim() const
{
    return isInvisiprim(mID);
}

//static
bool LLViewerTexture::isInvisiprim(const LLUUID& id)
{
    return (id == sInvisiprimTexture1) || (id == sInvisiprimTexture2);
}

//virtual
void LLViewerTexture::updateBindStatsForTester()
{
    LLTexturePipelineTester* tester = (LLTexturePipelineTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
    if (tester)
    {
        tester->updateTextureBindingStats(this);
    }
}

//----------------------------------------------------------------------------------------------
//end of LLViewerTexture
//----------------------------------------------------------------------------------------------

const std::string& fttype_to_string(const FTType& fttype)
{
    static const std::string ftt_unknown("FTT_UNKNOWN");
    static const std::string ftt_default("FTT_DEFAULT");
    static const std::string ftt_server_bake("FTT_SERVER_BAKE");
    static const std::string ftt_host_bake("FTT_HOST_BAKE");
    static const std::string ftt_map_tile("FTT_MAP_TILE");
    static const std::string ftt_local_file("FTT_LOCAL_FILE");
    static const std::string ftt_error("FTT_ERROR");
    switch(fttype)
    {
        case FTT_UNKNOWN: return ftt_unknown; break;
        case FTT_DEFAULT: return ftt_default; break;
        case FTT_SERVER_BAKE: return ftt_server_bake; break;
        case FTT_HOST_BAKE: return ftt_host_bake; break;
        case FTT_MAP_TILE: return ftt_map_tile; break;
        case FTT_LOCAL_FILE: return ftt_local_file; break;
    }
    return ftt_error;
}

//----------------------------------------------------------------------------------------------
//start of LLViewerFetchedTexture
//----------------------------------------------------------------------------------------------

//static
LLViewerFetchedTexture* LLViewerFetchedTexture::getSmokeImage()
{
    if (sSmokeImagep.isNull())
    {
        sSmokeImagep = LLViewerTextureManager::getFetchedTexture(IMG_SMOKE);
    }

    sSmokeImagep->addTextureStats(1024.f * 1024.f);

    return sSmokeImagep;
}

LLViewerFetchedTexture::LLViewerFetchedTexture(const LLUUID& id, FTType f_type, const LLHost& host, bool usemipmaps)
    : LLViewerTexture(id, usemipmaps),
    mTargetHost(host)
{
    init(true);
    mFTType = f_type;
    if (mFTType == FTT_HOST_BAKE)
    {
        // <FS:Ansariel> [Legacy Bake]
        //LL_WARNS() << "Unsupported fetch type " << mFTType << LL_ENDL;
        mCanUseHTTP = false;
        // </FS:Ansariel> [Legacy Bake]
    }
    generateGLTexture();
}

LLViewerFetchedTexture::LLViewerFetchedTexture(const LLImageRaw* raw, FTType f_type, bool usemipmaps)
    : LLViewerTexture(raw, usemipmaps)
{
    init(true);
    mFTType = f_type;
}

LLViewerFetchedTexture::LLViewerFetchedTexture(const std::string& url, FTType f_type, const LLUUID& id, bool usemipmaps)
    : LLViewerTexture(id, usemipmaps),
    mUrl(url)
{
    init(true);
    mFTType = f_type;
    generateGLTexture();
}

void LLViewerFetchedTexture::init(bool firstinit)
{
    mOrigWidth = 0;
    mOrigHeight = 0;
    mHasAux = false;
    mNeedsAux = false;
    mRequestedDiscardLevel = -1;
    mRequestedDownloadPriority = 0.f;
    mFullyLoaded = false;
    mCanUseHTTP = true;
    mDesiredDiscardLevel = MAX_DISCARD_LEVEL + 1;
    mMinDesiredDiscardLevel = MAX_DISCARD_LEVEL;

    mDecodingAux = false;

    mKnownDrawWidth = 0;
    mKnownDrawHeight = 0;
    mKnownDrawSizeChanged = false;

    if (firstinit)
    {
        mInImageList = 0;
    }

    // Only set mIsMissingAsset true when we know for certain that the database
    // does not contain this image.
    mIsMissingAsset = false;

    mLoadedCallbackDesiredDiscardLevel = S8_MAX;
    mPauseLoadedCallBacks = false;

    mNeedsCreateTexture = false;

    mIsRawImageValid = false;
    mRawDiscardLevel = INVALID_DISCARD_LEVEL;
    mMinDiscardLevel = 0;
    mMaxFaceImportance = 1.f;

    mHasFetcher = false;
    mIsFetching = false;
    mFetchState = 0;
    mFetchPriority = 0;
    mDownloadProgress = 0.f;
    mFetchDeltaTime = 999999.f;
    mRequestDeltaTime = 0.f;
    mForSculpt = false;
    mForHUD = false;
    mForParticle = false;
    mIsFetched = false;
    mInFastCacheList = false;

    mSavedRawImage = NULL;
    mForceToSaveRawImage  = false;
    mSaveRawImage = false;
    mSavedRawDiscardLevel = -1;
    mDesiredSavedRawDiscardLevel = -1;
    mLastReferencedSavedRawImageTime = 0.0f;
    mKeptSavedRawImageTime = 0.f;
    mLastCallBackActiveTime = 0.f;
    mForceCallbackFetch = false;
    // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
    mCloseToCamera = 1.0f; // Store if the camera is close to the camera (0.0f or 1.0f)
    // </FS:minerjr> [FIRE-35081]

    mFTType = FTT_UNKNOWN;
    mBoostLoaded = 0;
    mLastTimeUpdated.start();
}

LLViewerFetchedTexture::~LLViewerFetchedTexture()
{
    assert_main_thread();
    //*NOTE getTextureFetch can return NULL when Viewer is shutting down.
    // This is due to LLWearableList is singleton and is destroyed after
    // LLAppViewer::cleanup() was called. (see ticket EXT-177)
    if (mHasFetcher && LLAppViewer::getTextureFetch())
    {
        LLAppViewer::getTextureFetch()->deleteRequest(getID(), true);
    }
    cleanup();
}

//virtual
S8 LLViewerFetchedTexture::getType() const
{
    return LLViewerTexture::FETCHED_TEXTURE;
}

FTType LLViewerFetchedTexture::getFTType() const
{
    return mFTType;
}

void LLViewerFetchedTexture::cleanup()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
        iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter++;
        // We never finished loading the image.  Indicate failure.
        // Note: this allows mLoadedCallbackUserData to be cleaned up.
        entryp->mCallback( false, this, NULL, NULL, 0, true, entryp->mUserData );
        entryp->removeTexture(this);
        delete entryp;
    }
    mLoadedCallbackList.clear();
    mNeedsAux = false;

    // Clean up image data
    destroyRawImage();
    mSavedRawImage = NULL;
    mSavedRawDiscardLevel = -1;
}

//access the fast cache
void LLViewerFetchedTexture::loadFromFastCache()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if(!mInFastCacheList)
    {
        return; //no need to access the fast cache.
    }
    mInFastCacheList = false;

    add(LLTextureFetch::sCacheAttempt, 1.0);

    LLTimer fastCacheTimer;
    mRawImage = LLAppViewer::getTextureCache()->readFromFastCache(getID(), mRawDiscardLevel);
    if(mRawImage.notNull())
    {
        F32 cachReadTime = fastCacheTimer.getElapsedTimeF32();

        add(LLTextureFetch::sCacheHit, 1.0);
        record(LLTextureFetch::sCacheHitRate, LLUnits::Ratio::fromValue(1));
        sample(LLTextureFetch::sCacheReadLatency, cachReadTime);

        mFullWidth  = mRawImage->getWidth()  << mRawDiscardLevel;
        mFullHeight = mRawImage->getHeight() << mRawDiscardLevel;
        setTexelsPerImage();

        if(mFullWidth > MAX_IMAGE_SIZE || mFullHeight > MAX_IMAGE_SIZE)
        {
            //discard all oversized textures.
            destroyRawImage();
            LL_WARNS() << "oversized, setting as missing" << LL_ENDL;
            setIsMissingAsset();
            mRawDiscardLevel = INVALID_DISCARD_LEVEL;
        }
        else
        {
            if (mBoostLevel == LLGLTexture::BOOST_ICON)
            {
                // Shouldn't do anything usefull since texures in fast cache are 16x16,
                // it is here in case fast cache changes.
                S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_ICON_DIMENSIONS;
                S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_ICON_DIMENSIONS;
                if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                {
                    // scale oversized icon, no need to give more work to gl
                    mRawImage->scale(expected_width, expected_height);
                }
            }

            if (mBoostLevel == LLGLTexture::BOOST_THUMBNAIL)
            {
                S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_THUMBNAIL_DIMENSIONS;
                S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_THUMBNAIL_DIMENSIONS;
                if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                {
                    // scale oversized icon, no need to give more work to gl
                    mRawImage->scale(expected_width, expected_height);
                }
            }

            mRequestedDiscardLevel = mDesiredDiscardLevel + 1;
            mIsRawImageValid = true;
            addToCreateTexture();
        }
    }
    else
    {
        record(LLTextureFetch::sCacheHitRate, LLUnits::Ratio::fromValue(0));
    }
}

void LLViewerFetchedTexture::setForSculpt()
{
    //static const S32 MAX_INTERVAL = 8; //frames

    //forceToSaveRawImage(0, F32_MAX);

    setBoostLevel(llmax((S32)getBoostLevel(),
        (S32)LLGLTexture::BOOST_SCULPTED));

    mForSculpt = true;
    if(isForSculptOnly() && hasGLTexture() && !getBoundRecently())
    {
        destroyGLTexture(); //sculpt image does not need gl texture.
        mTextureState = ACTIVE;
    }
    //checkCachedRawSculptImage();
    //setMaxVirtualSizeResetInterval(MAX_INTERVAL);
}

bool LLViewerFetchedTexture::isForSculptOnly() const
{
    return mForSculpt && !mNeedsGLTexture;
}

bool LLViewerFetchedTexture::isDeleted()
{
    return mTextureState == DELETED;
}

bool LLViewerFetchedTexture::isInactive()
{
    return mTextureState == INACTIVE;
}

bool LLViewerFetchedTexture::isDeletionCandidate()
{
    return mTextureState == DELETION_CANDIDATE;
}

bool LLViewerFetchedTexture::isActive()
{
    return mTextureState >= ACTIVE;
}

void LLViewerFetchedTexture::setDeletionCandidate()
{
    if(mGLTexturep.notNull() && mGLTexturep->getTexName() && (mTextureState == INACTIVE))
    {
        mTextureState = DELETION_CANDIDATE;
    }
}

//set the texture inactive
// <TS:3T> Adjusted function to allow mTextureState to be flipped back to Active if sent a true bool.
void LLViewerFetchedTexture::setInactive(bool found)
{
    if(mTextureState > DELETED && mTextureState != NO_DELETE && mGLTexturep.notNull() && mGLTexturep->getTexName() && !mGLTexturep->getBoundRecently())
    {
        if (found) {
            mTextureState = ACTIVE;
        }
        else {
            if (mTextureState == ACTIVE) // Only active textures will be set inactive.
                mTextureState = INACTIVE;
        }
    }
}
// </TS:3T>

bool LLViewerFetchedTexture::isFullyLoaded() const
{
    // Unfortunately, the boolean "mFullyLoaded" is never updated correctly so we use that logic
    // to check if the texture is there and completely downloaded
    return (mFullWidth != 0) && (mFullHeight != 0) && !mIsFetching && !mHasFetcher;
}


// virtual
void LLViewerFetchedTexture::dump()
{
    LLViewerTexture::dump();

    LL_INFOS() << "Dump : " << mID
            << ", mIsMissingAsset = " << (S32)mIsMissingAsset
            << ", mFullWidth = " << (S32)mFullWidth
            << ", mFullHeight = " << (S32)mFullHeight
            << ", mOrigWidth = " << (S32)mOrigWidth
            << ", mOrigHeight = " << (S32)mOrigHeight
            << LL_ENDL;
    LL_INFOS() << "     : "
            << " mFullyLoaded = " << (S32)mFullyLoaded
            << ", mFetchState = " << (S32)mFetchState
            << ", mFetchPriority = " << (S32)mFetchPriority
            << ", mDownloadProgress = " << (F32)mDownloadProgress
            << LL_ENDL;
    LL_INFOS() << "     : "
            << " mHasFetcher = " << (S32)mHasFetcher
            << ", mIsFetching = " << (S32)mIsFetching
            << ", mIsFetched = " << (S32)mIsFetched
            << ", mBoostLevel = " << (S32)mBoostLevel
            << LL_ENDL;
}

///////////////////////////////////////////////////////////////////////////////
// ONLY called from LLViewerFetchedTextureList
void LLViewerFetchedTexture::destroyTexture()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (mNeedsCreateTexture)//return if in the process of generating a new texture.
    {
        return;
    }
    // <3T:TommyTheTerrible> Make sure texture is removed from Fetching texture list when destroyed.
    gTextureList.mFetchingTextures.erase(this);
    // </3T>
    //LL_DEBUGS("Avatar") << mID << LL_ENDL;
    destroyGLTexture();
    mFullyLoaded = false;
}

void LLViewerFetchedTexture::addToCreateTexture()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    bool force_update = false;
    if (getComponents() != mRawImage->getComponents())
    {
        // We've changed the number of components, so we need to move any
        // objects using this pool to a different pool.
        mComponents = mRawImage->getComponents();
        mGLTexturep->setComponents(mComponents);
        force_update = true;

        for (U32 j = 0; j < LLRender::NUM_TEXTURE_CHANNELS; ++j)
        {
            llassert(mNumFaces[j] <= mFaceList[j].size());

            for(U32 i = 0; i < mNumFaces[j]; i++)
            {
                mFaceList[j][i]->dirtyTexture();
            }
        }

        mSavedRawDiscardLevel = -1;
        mSavedRawImage = NULL;
    }

    if(isForSculptOnly())
    {
        //just update some variables, not to create a real GL texture.
        createGLTexture(mRawDiscardLevel, mRawImage, 0, false);
        mNeedsCreateTexture = false;
        destroyRawImage();
    }
    //<TS:3T> Stop expecting all new discards to be lower
    //else if(!force_update && getDiscardLevel() > -1 && getDiscardLevel() <= mRawDiscardLevel)
    //{
    //    mNeedsCreateTexture = false;
    //    destroyRawImage();
    //}
    // </TS:3T>
    else
    {
        scheduleCreateTexture();
    }
    return;
}

// ONLY called from LLViewerTextureList
bool LLViewerFetchedTexture::preCreateTexture(S32 usename/*= 0*/)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
#if LL_IMAGEGL_THREAD_CHECK
    mGLTexturep->checkActiveThread();
#endif

    if (!mNeedsCreateTexture)
    {
        destroyRawImage();
        return false;
    }
    mNeedsCreateTexture = false;

    if (mRawImage.isNull())
    {
        LL_ERRS() << "LLViewerTexture trying to create texture with no Raw Image" << LL_ENDL;
    }
    if (mRawImage->isBufferInvalid())
    {
        LL_WARNS() << "Can't create a texture: invalid image data" << LL_ENDL;
        destroyRawImage();
        return false;
    }
    //  LL_INFOS() << llformat("IMAGE Creating (%d) [%d x %d] Bytes: %d ",
    //                      mRawDiscardLevel,
    //                      mRawImage->getWidth(), mRawImage->getHeight(),mRawImage->getDataSize())
    //          << mID.getString() << LL_ENDL;

    // <FS:Techwolf Lupindo> texture comment metadata reader
    if (!mRawImage->mComment.empty())
    {
        std::string comment = mRawImage->mComment;
        mComment["comment"] = comment;
        std::size_t position = 0;
        std::size_t length = comment.length();
        while (position < length)
        {
            std::size_t equals_position = comment.find("=", position);
            if (equals_position != std::string::npos)
            {
                std::string type = comment.substr(position, equals_position - position);
                position = comment.find("&", position);
                if (position != std::string::npos)
                {
                    mComment[type] = comment.substr(equals_position + 1, position - (equals_position + 1));
                    position++;
                }
                else
                {
                    mComment[type] = comment.substr(equals_position + 1, length - (equals_position + 1));
                }
            }
            else
            {
                position = equals_position;
            }
        }
    }
    // </FS:Techwolf Lupindo>

    bool res = true;

    // store original size only for locally-sourced images
    if (mUrl.compare(0, 7, "file://") == 0)
    {
        mOrigWidth = mRawImage->getWidth();
        mOrigHeight = mRawImage->getHeight();

        // This is only safe because it's a local image and fetcher doesn't use raw data
        // from local images, but this might become unsafe in case of changes to fetcher
        if (mBoostLevel == BOOST_PREVIEW)
        {
            mRawImage->biasedScaleToPowerOfTwo(1024);
        }
        else
        { // leave black border, do not scale image content
            mRawImage->expandToPowerOfTwo(MAX_IMAGE_SIZE, false);
        }

        mFullWidth = mRawImage->getWidth();
        mFullHeight = mRawImage->getHeight();
        setTexelsPerImage();
    }
    else
    {
        mOrigWidth = mFullWidth;
        mOrigHeight = mFullHeight;
    }

    bool size_okay = true;

    S32 discard_level = mRawDiscardLevel;
    if (mRawDiscardLevel < 0)
    {
        LL_DEBUGS() << "Negative raw discard level when creating image: " << mRawDiscardLevel << LL_ENDL;
        discard_level = 0;
    }

    U32 raw_width = mRawImage->getWidth() << discard_level;
    U32 raw_height = mRawImage->getHeight() << discard_level;

    if (raw_width > MAX_IMAGE_SIZE || raw_height > MAX_IMAGE_SIZE)
    {
        LL_INFOS() << "Width or height is greater than " << MAX_IMAGE_SIZE << ": (" << raw_width << "," << raw_height << ")" << LL_ENDL;
        size_okay = false;
    }

    if (!LLImageGL::checkSize(mRawImage->getWidth(), mRawImage->getHeight()))
    {
        // A non power-of-two image was uploaded (through a non standard client)
        LL_INFOS() << "Non power of two width or height: (" << mRawImage->getWidth() << "," << mRawImage->getHeight() << ")" << LL_ENDL;
        size_okay = false;
    }

    if (!size_okay)
    {
        // An inappropriately-sized image was uploaded (through a non standard client)
        // We treat these images as missing assets which causes them to
        // be renderd as 'missing image' and to stop requesting data
        LL_WARNS() << "!size_ok, setting as missing" << LL_ENDL;
        setIsMissingAsset();
        destroyRawImage();
        return false;
    }

    if (mGLTexturep->getHasExplicitFormat())
    {
        LLGLenum format = mGLTexturep->getPrimaryFormat();
        S8 components = mRawImage->getComponents();
        if ((format == GL_RGBA && components < 4)
            || (format == GL_RGB && components < 3))
        {
            LL_WARNS() << "Can't create a texture " << mID << ": invalid image format " << std::hex << format << " vs components " << (U32)components << LL_ENDL;
            // Was expecting specific format but raw texture has insufficient components for
            // such format, using such texture will result in crash or will display wrongly
            // if we change format. Texture might be corrupted server side, so just set as
            // missing and clear cashed texture (do not cause reload loop, will retry&recover
            // during new session)
            setIsMissingAsset();
            destroyRawImage();
            LLAppViewer::getTextureCache()->removeFromCache(mID);
            return false;
        }
    }

    return res;
}

bool LLViewerFetchedTexture::createTexture(S32 usename/*= 0*/)
{
    if (!mNeedsCreateTexture)
    {
        return false;
    }

    bool res = mGLTexturep->createGLTexture(mRawDiscardLevel, mRawImage, usename, true, mBoostLevel);

    return res;
}

void LLViewerFetchedTexture::postCreateTexture()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (!mNeedsCreateTexture)
    {
        return;
    }
#if LL_IMAGEGL_THREAD_CHECK
    mGLTexturep->checkActiveThread();
#endif

    setActive();

    // rebuild any volumes that are using this texture for sculpts in case their LoD has changed
    for (U32 i = 0; i < mNumVolumes[LLRender::SCULPT_TEX]; ++i)
    {
        LLVOVolume* volume = mVolumeList[LLRender::SCULPT_TEX][i];
        if (volume)
        {
            volume->mSculptChanged = true;
            gPipeline.markRebuild(volume->mDrawable);
        }
    }

    if (!needsToSaveRawImage())
    {
        mNeedsAux = false;
    }
    if (getBoostLevel() > 0)
        mBoostLoaded++;
    mNeedsCreateTexture = false;
}

void LLViewerFetchedTexture::scheduleCreateTexture()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (!mNeedsCreateTexture)
    {
        mNeedsCreateTexture = true;
        if (preCreateTexture())
        {
#if LL_IMAGEGL_THREAD_CHECK
            //grab a copy of the raw image data to make sure it isn't modified pending texture creation
            U8* data = mRawImage->getData();
            U8* data_copy = nullptr;
            S32 size = mRawImage->getDataSize();
            if (data != nullptr && size > 0)
            {
                data_copy = new U8[size];
                memcpy(data_copy, data, size);
            }
#endif
            mNeedsCreateTexture = true;
            auto mainq = LLImageGLThread::sEnabledTextures ? mMainQueue.lock() : nullptr;
            if (mainq)
            {
                ref();
                mainq->postTo(
                    mImageQueue,
                    // work to be done on LLImageGL worker thread
#if LL_IMAGEGL_THREAD_CHECK
                    [this, data, data_copy, size]()
                    {
                        mGLTexturep->mActiveThread = LLThread::currentID();
                        //verify data is unmodified
                        llassert(data == mRawImage->getData());
                        llassert(mRawImage->getDataSize() == size);
                        llassert(memcmp(data, data_copy, size) == 0);
#else
                    [this]()
                    {
#endif
                        //actually create the texture on a background thread
                        createTexture();

#if LL_IMAGEGL_THREAD_CHECK
                        //verify data is unmodified
                        llassert(data == mRawImage->getData());
                        llassert(mRawImage->getDataSize() == size);
                        llassert(memcmp(data, data_copy, size) == 0);
#endif
                    },
                    // callback to be run on main thread
#if LL_IMAGEGL_THREAD_CHECK
                        [this, data, data_copy, size]()
                    {
                        mGLTexturep->mActiveThread = LLThread::currentID();
                        llassert(data == mRawImage->getData());
                        llassert(mRawImage->getDataSize() == size);
                        llassert(memcmp(data, data_copy, size) == 0);
                        delete[] data_copy;
#else
                        [this]()
                        {
#endif
                        //finalize on main thread
                        postCreateTexture();
                        unref();
                    });
            }
            else
            {
                if (!mCreatePending)
                {
                    mCreatePending = true;
                    gTextureList.mCreateTextureList.push(this);
                }
            }
        }
    }
}

// Call with 0,0 to turn this feature off.
//virtual
void LLViewerFetchedTexture::setKnownDrawSize(S32 width, S32 height)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (width > 0 && height > 0 && (mKnownDrawWidth != width || mKnownDrawHeight != height))  // <TS:3T> Allow mKnowns to be set to anything so long as different.
    {
        mKnownDrawWidth = width;
        mKnownDrawHeight = height;
        mKnownDrawSizeChanged = true;
        mFullyLoaded = false;
    }
    addTextureStats((F32)(mKnownDrawWidth * mKnownDrawHeight));
}

void LLViewerFetchedTexture::setDebugText(const std::string& text)
{
    for (U32 i = 0; i < LLRender::NUM_TEXTURE_CHANNELS; ++i)
    {
        for (S32 fi = 0; fi < getNumFaces(i); ++fi)
        {
            LLFace* facep = (*(getFaceList(i)))[fi];

            if (facep)
            {
                LLDrawable* drawable = facep->getDrawable();
                if (drawable)
                {
                    drawable->getVObj()->setDebugText(text);
                }
            }
        }
    }
}

extern bool gCubeSnapshot;

//virtual
void LLViewerFetchedTexture::processTextureStats()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    llassert(!gCubeSnapshot);  // should only be called when the main camera is active
    llassert(!LLPipeline::sShadowRender);

    if(mFullyLoaded)
    {
        if(mDesiredDiscardLevel > mMinDesiredDiscardLevel)//need to load more
        {
            mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, mMinDesiredDiscardLevel);
            mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S32)mLoadedCallbackDesiredDiscardLevel);
            mFullyLoaded = false;
        }
        //setDebugText("fully loaded");
    }
    else
    {
        updateVirtualSize();

        static LLCachedControl<bool> textures_fullres(gSavedSettings,"TextureLoadFullRes", false);

        U32 max_tex_res = MAX_IMAGE_SIZE_DEFAULT;
        if (mBoostLevel < LLGLTexture::BOOST_HIGH)
        {
            // restrict texture resolution to download based on RenderMaxTextureResolution
            static LLCachedControl<U32> max_texture_resolution(gSavedSettings, "RenderMaxTextureResolution", 2048);
            // sanity clamp debug setting to avoid settings hack shenanigans
            max_tex_res = (U32)llclamp((U32)max_texture_resolution, 512, MAX_IMAGE_SIZE_DEFAULT);
            mMaxVirtualSize = llmin(mMaxVirtualSize, (F32)(max_tex_res * max_tex_res));
        }

        if (textures_fullres)
        {
            mDesiredDiscardLevel = 0;
        }
        else if (mDontDiscard && (mBoostLevel == LLGLTexture::BOOST_ICON || mBoostLevel == LLGLTexture::BOOST_THUMBNAIL))
        {
            if (mFullWidth > MAX_IMAGE_SIZE_DEFAULT || mFullHeight > MAX_IMAGE_SIZE_DEFAULT)
            {
                mDesiredDiscardLevel = 1; // MAX_IMAGE_SIZE_DEFAULT = 2048 and max size ever is 4096
            }
            else
            {
                mDesiredDiscardLevel = 0;
            }
        }
        else if(!mFullWidth || !mFullHeight)
        {
            mDesiredDiscardLevel =  llmin(getMaxDiscardLevel(), (S32)mLoadedCallbackDesiredDiscardLevel);
        }
        else
        {
            if(!mKnownDrawWidth || !mKnownDrawHeight || (S32)mFullWidth <= mKnownDrawWidth || (S32)mFullHeight <= mKnownDrawHeight)
            {
                if (mFullWidth > max_tex_res || mFullHeight > max_tex_res)
                {
                    mDesiredDiscardLevel = 1;
                }
                else
                {
                    mDesiredDiscardLevel = 0;
                }
            }
            else if(mKnownDrawSizeChanged)//known draw size is set
            {
                mDesiredDiscardLevel = (S8)llmin(log((F32)mFullWidth / mKnownDrawWidth) / log_2,
                                                     log((F32)mFullHeight / mKnownDrawHeight) / log_2);
                mDesiredDiscardLevel =  llclamp(mDesiredDiscardLevel, (S8)0, (S8)getMaxDiscardLevel());
                mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, mMinDesiredDiscardLevel);
                mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S32)mLoadedCallbackDesiredDiscardLevel);
            }
            mKnownDrawSizeChanged = false;

            if(getDiscardLevel() >= 0 && (getDiscardLevel() == mDesiredDiscardLevel)) // <TS:3T> Stop expecting all new discards to be lower
            {
                mFullyLoaded = true;
            }
        }
    }

    if(mForceToSaveRawImage && mDesiredSavedRawDiscardLevel >= 0) //force to refetch the texture.
    {
        mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S8)mDesiredSavedRawDiscardLevel);
        if(getDiscardLevel() < 0 || getDiscardLevel() > mDesiredDiscardLevel)
        {
            mFullyLoaded = false;
        }
    }
}

//============================================================================

void LLViewerFetchedTexture::updateVirtualSize()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    reorganizeFaceList();
    reorganizeVolumeList();
}

S32 LLViewerFetchedTexture::getCurrentDiscardLevelForFetching()
{
    S32 current_discard = getDiscardLevel();
    if(mForceToSaveRawImage)
    {
        if(mSavedRawDiscardLevel < 0 || current_discard < 0)
        {
            current_discard = -1;
        }
        else
        {
            current_discard = llmax(current_discard, mSavedRawDiscardLevel);
        }
    }

    return current_discard;
}

bool LLViewerFetchedTexture::isActiveFetching()
{
    static LLCachedControl<bool> monitor_enabled(gSavedSettings,"DebugShowTextureInfo");

    // <FS:Ansariel> OpenSim compatibility
    //return mFetchState > 7 && mFetchState < 10 && monitor_enabled; //in state of WAIT_HTTP_REQ or DECODE_IMAGE.
    return mFetchState > 8 && mFetchState < 11 && monitor_enabled; //in state of WAIT_HTTP_REQ or DECODE_IMAGE.
}

void LLViewerFetchedTexture::setBoostLevel(S32 level)
{
    LLViewerTexture::setBoostLevel(level);

    if (level > LLViewerTexture::BOOST_HIGH)
    {
        mDesiredDiscardLevel = 0;
    }
}

bool LLViewerFetchedTexture::processFetchResults(S32& desired_discard, S32 current_discard, S32 fetch_discard, F32 decode_priority)
{
    // We may have data ready regardless of whether or not we are finished (e.g. waiting on write)
    if (mRawImage.notNull())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - has raw image");
        LLTexturePipelineTester* tester = (LLTexturePipelineTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
        if (tester)
        {
            mIsFetched = true;
            tester->updateTextureLoadingStats(this, mRawImage, LLAppViewer::getTextureFetch()->isFromLocalCache(mID));
        }
        mRawDiscardLevel = fetch_discard;
        if ((mRawImage->getDataSize() > 0 && mRawDiscardLevel >= 0) &&
            (current_discard < 0 || mRawDiscardLevel < current_discard))
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - data good");

            // This is going to conflict with Develop, just pick from develop
            // where it uses setDimensions instead of setTexelsPerImage
            mFullWidth = mRawImage->getWidth() << mRawDiscardLevel;
            mFullHeight = mRawImage->getHeight() << mRawDiscardLevel;
            setTexelsPerImage();

            if (mFullWidth > MAX_IMAGE_SIZE || mFullHeight > MAX_IMAGE_SIZE)
            {
                //discard all oversized textures.
                destroyRawImage();
                LL_WARNS() << "oversize, setting as missing" << LL_ENDL;
                setIsMissingAsset();
                mRawDiscardLevel = INVALID_DISCARD_LEVEL;
                mIsFetching = false;
                mLastPacketTimer.reset();
            }
            else
            {
                mIsRawImageValid = true;
                addToCreateTexture();
            }

            if (mBoostLevel == LLGLTexture::BOOST_ICON)
            {
                S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_ICON_DIMENSIONS;
                S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_ICON_DIMENSIONS;
                if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                {
                    // scale oversized icon, no need to give more work to gl
                    // since we got mRawImage from thread worker and image may be in use (ex: writing cache), make a copy
                    //
                    // BOOST_ICON gets scaling because profile icons can have a bunch of different formats, not just j2c
                    // Might need another pass to use discard for j2c and scaling for everything else.
                    mRawImage = mRawImage->scaled(expected_width, expected_height);
                }
            }

            if (mBoostLevel == LLGLTexture::BOOST_THUMBNAIL)
            {
                S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_THUMBNAIL_DIMENSIONS;
                S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_THUMBNAIL_DIMENSIONS;
                if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                {
                    // scale oversized icon, no need to give more work to gl
                    // since we got mRawImage from thread worker and image may be in use (ex: writing cache), make a copy
                    //
                    // Todo: probably needs to be remade to use discard, all thumbnails are supposed to be j2c,
                    // so no need to scale, should be posible to use discard to scale image down.
                    mRawImage = mRawImage->scaled(expected_width, expected_height);
                }
            }

            return true;
        }
        else
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - data not needed");
            // Data is ready but we don't need it
            // (received it already while fetcher was writing to disk)
            destroyRawImage();
            return false; // done
        }
    }

    if (!mIsFetching)
    {
        if ((decode_priority > 0)
            && (mRawDiscardLevel < 0 || mRawDiscardLevel == INVALID_DISCARD_LEVEL)
            && mFetchState > 1) // 1 - initial, make sure fetcher did at least something
        {
            // We finished but received no data
            if (getDiscardLevel() < 0)
            {
                if (getFTType() != FTT_MAP_TILE)
                {
                    LL_WARNS() << mID
                        << " Fetch failure, setting as missing, decode_priority " << decode_priority
                        << " mRawDiscardLevel " << mRawDiscardLevel
                        << " current_discard " << current_discard
                        << " stats " << mLastHttpGetStatus.toHex()
                        << " worker state " << mFetchState
                        << LL_ENDL;
                }
                setIsMissingAsset();
                desired_discard = -1;
            }
            else
            {
                //LL_WARNS() << mID << ": Setting min discard to " << current_discard << LL_ENDL;
                if (current_discard >= 0)
                {
                    mMinDiscardLevel = current_discard;
                    //desired_discard = current_discard;
                }
                else
                {
                    S32 dis_level = getDiscardLevel();
                    mMinDiscardLevel = dis_level;
                    //desired_discard = dis_level;
                }
            }
            destroyRawImage();
        }
        else if (mRawImage.notNull())
        {
            // We have data, but our fetch failed to return raw data
            // *TODO: FIgure out why this is happening and fix it
            // Potentially can happen when TEX_LIST_SCALE and TEX_LIST_STANDARD
            // get requested for the same texture id at the same time
            // (two textures, one fetcher)
            destroyRawImage();
        }
    }

    return true;
}

bool LLViewerFetchedTexture::updateFetch()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static LLCachedControl<bool> textures_decode_disabled(gSavedSettings,"TextureDecodeDisabled", false);

    if(textures_decode_disabled) // don't fetch the surface textures in wireframe mode
    {
        return false;
    }
    //<3T:TommyTheTerrible> Updated the desired discard before checking if fetch is necessary.
    //      This was originally in LLViewerTextureList::updateImageDecodePriority, which is called less frequently now.
    processTextureStats();
    //</3T>

    mFetchState = 0;
    mFetchPriority = 0;
    mFetchDeltaTime = 999999.f;
    mRequestDeltaTime = 999999.f;

#ifndef LL_RELEASE_FOR_DOWNLOAD
    if (mID == LLAppViewer::getTextureFetch()->mDebugID)
    {
        LLAppViewer::getTextureFetch()->mDebugCount++; // for setting breakpoints
    }
#endif

    if (mNeedsCreateTexture)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - needs create");
        // We may be fetching still (e.g. waiting on write)
        // but don't check until we've processed the raw data we have
        return false;
    }
    if (mIsMissingAsset)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - missing asset");
        llassert(!mHasFetcher);
        // <3T:TommyTheTerrible> Remove from Fetching texture list if missing.
        LLViewerFetchedTexture& unref = *this; // Need to do this to reduce the references on the texture.
        gTextureList.mFetchingTextures.erase(this);
        // </3T:TommyTheTerrible>
        return false; // skip
    }
    if (!mLoadedCallbackList.empty() && mRawImage.notNull())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - callback pending");
        return false; // process any raw image data in callbacks before replacing
    }
    if(mInFastCacheList)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - in fast cache");
        return false;
    }
    if (mGLTexturep.isNull())
    { // fix for crash inside getCurrentDiscardLevelForFetching (shouldn't happen but appears to be happening)
        llassert(false);
        return false;
    }

    // S32 current_discard = getCurrentDiscardLevelForFetching();
    S32 current_discard = getDiscardLevel();
    S32 desired_discard = getDesiredDiscardLevel();
    F32 decode_priority = mMaxVirtualSize;
    F32 importance      = getMaxFaceImportance();

    if (forParticle())
        decode_priority = (4096 * 4096);
    decode_priority = llmin(decode_priority, LLViewerFetchedTexture::sMaxVirtualSize);
    // </TS:3T>

    if (mIsFetching)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - is fetching");
        // Sets mRawDiscardLevel, mRawImage, mAuxRawImage
        S32 fetch_discard = current_discard;

        if (mRawImage.notNull()) sRawCount--;
        if (mAuxRawImage.notNull()) sAuxCount--;
        // keep in mind that fetcher still might need raw image, don't modify original
        bool finished = LLAppViewer::getTextureFetch()->getRequestFinished(getID(), fetch_discard, mFetchState, mRawImage, mAuxRawImage,
                                                                           mLastHttpGetStatus);
        if (mRawImage.notNull()) sRawCount++;
        if (mAuxRawImage.notNull())
        {
            mHasAux = true;
            sAuxCount++;
        }
        if (finished)
        {
            mIsFetching = false;
            // mLastFetchState = -1; <TS:3T> Do not reset, to keep track of last fetch response.
            mLastPacketTimer.reset();
            mLastTimeUpdated.reset();
        }
        else
        {
            mFetchState = LLAppViewer::getTextureFetch()->getFetchState(mID, mDownloadProgress, mRequestedDownloadPriority,
                                                                        mFetchPriority, mFetchDeltaTime, mRequestDeltaTime, mCanUseHTTP);
        }

        // We may have data ready regardless of whether or not we are finished (e.g. waiting on write)
        if (mRawImage.notNull())
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - has raw image");
            LLTexturePipelineTester* tester = (LLTexturePipelineTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
            if (tester)
            {
                mIsFetched = true;
                tester->updateTextureLoadingStats(this, mRawImage, LLAppViewer::getTextureFetch()->isFromLocalCache(mID));
            }
            mRawDiscardLevel = fetch_discard;
            if ((mRawImage->getDataSize() > 0 && mRawDiscardLevel >= 0) &&
                (current_discard < 0 || mRawDiscardLevel != current_discard)) // <TS:3T> Stop expecting all new discards to always be lower
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - data good");
                mFullWidth  = mRawImage->getWidth() << mRawDiscardLevel;
                mFullHeight = mRawImage->getHeight() << mRawDiscardLevel;
                setTexelsPerImage();

                if (mFullWidth > MAX_IMAGE_SIZE || mFullHeight > MAX_IMAGE_SIZE)
                {
                    // discard all oversized textures.
                    LL_INFOS() << "Discarding oversized texture, width= " << mFullWidth << ", height= " << mFullHeight << LL_ENDL;
                    destroyRawImage();
                    LL_WARNS() << "oversize, setting as missing" << LL_ENDL;
                    setIsMissingAsset();
                    mRawDiscardLevel = INVALID_DISCARD_LEVEL;
                    mIsFetching      = false;
                    mLastPacketTimer.reset();
                }
                else
                {
                    mIsRawImageValid = true;
                    addToCreateTexture();
                }

                if (mBoostLevel == LLGLTexture::BOOST_ICON)
                {
                    S32 expected_width  = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_ICON_DIMENSIONS;
                    S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_ICON_DIMENSIONS;
                    if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                    {
                        // scale oversized icon, no need to give more work to gl
                        // since we got mRawImage from thread worker and image may be in use (ex: writing cache), make a copy
                        mRawImage = mRawImage->scaled(expected_width, expected_height);
                    }
                }

                if (mBoostLevel == LLGLTexture::BOOST_THUMBNAIL)
                {
                    S32 expected_width  = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_THUMBNAIL_DIMENSIONS;
                    S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_THUMBNAIL_DIMENSIONS;
                    if (mRawImage && (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height))
                    {
                        // scale oversized icon, no need to give more work to gl
                        // since we got mRawImage from thread worker and image may be in use (ex: writing cache), make a copy
                        mRawImage = mRawImage->scaled(expected_width, expected_height);
                    }
                }

                return true;
            }
            else
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - data not needed");
                // Data is ready but we don't need it
                // (received it already while fetcher was writing to disk)
                destroyRawImage();
                return false; // done
            }
        }

        if (!mIsFetching)
        {
            if ((decode_priority > 0) && (mRawDiscardLevel < 0 || mRawDiscardLevel == INVALID_DISCARD_LEVEL))
            {
                // We finished but received no data
                if (getDiscardLevel() < 0)
                {
                    if (getFTType() != FTT_MAP_TILE)
                    {
                        LL_WARNS() << mID << " Fetch failure, setting as missing, decode_priority " << decode_priority
                                   << " mRawDiscardLevel " << mRawDiscardLevel << " current_discard " << current_discard << " stats "
                                   << mLastHttpGetStatus.toHex() << LL_ENDL;
                    }
                    setIsMissingAsset();
                    desired_discard = -1;
                }
                else
                {
                    // LL_WARNS() << mID << ": Setting min discard to " << current_discard << LL_ENDL;
                    if (current_discard >= 0)
                    {
                        mMinDiscardLevel = current_discard;
                        // desired_discard = current_discard;
                    }
                    else
                    {
                        S32 dis_level    = getDiscardLevel();
                        mMinDiscardLevel = dis_level;
                        // desired_discard = dis_level;
                    }
                }
                destroyRawImage();
            }
            else if (mRawImage.notNull())
            {
                // We have data, but our fetch failed to return raw data
                // *TODO: FIgure out why this is happening and fix it
                destroyRawImage();
            }
        }
        else
        {
            static const F32 MAX_HOLD_TIME = 5.0f; // seconds to wait before canceling fecthing if decode_priority is 0.f.
            if (decode_priority > 0.0f || mStopFetchingTimer.getElapsedTimeF32() > MAX_HOLD_TIME)
            {
                mStopFetchingTimer.reset();
                LLAppViewer::getTextureFetch()->updateRequestPriority(mID, decode_priority);
            }
        }
    }

    S32 fetchstate = LLAppViewer::getTextureFetch()->getFetchState(mID, mDownloadProgress, mRequestedDownloadPriority, mFetchPriority,
                                                                   mFetchDeltaTime, mRequestDeltaTime, mCanUseHTTP);
    if (fetchstate < 14) // LLTextureFetchWorker::INIT = 1, DONE = 14
        LLAppViewer::getTextureFetch()->updateRequestPriority(mID, decode_priority);

    desired_discard = llmin(desired_discard, getMaxDiscardLevel());

    // <FS:Ansariel> Replace frequently called gSavedSettings
    static LLCachedControl<U32> sTextureDiscardLevel(gSavedSettings, "TextureDiscardLevel");
    const U32                   override_tex_discard_level = sTextureDiscardLevel();
    // </FS:Ansariel>
    if (override_tex_discard_level != 0)
    {
        desired_discard = override_tex_discard_level;
    }

    bool make_request = true;
    if (decode_priority <= 0)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - priority <= 0");
        make_request = false;
    }
    else if (mDesiredDiscardLevel > getMaxDiscardLevel())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - desired > max");
        make_request = false;
    }
    else if (mNeedsCreateTexture || mIsMissingAsset)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - create or missing");
        make_request = false;
    }
    // <3T:TommyTheTerrible> Allow Baked Avatar textures to use discards above 0. 
    else if ((mBoostLevel > LLViewerTexture::BOOST_AVATAR_BAKED) && current_discard >= 0 &&
             current_discard <= desired_discard)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - do not LOD adjust Boost");
        make_request = false;
    }
    // </3T:TommyTheTerrible>
    // <3T:TommyTheTerrible> Stop unnecessary requests when already at desired discard. 
    else if (current_discard >= 0 && current_discard == desired_discard)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - Do not Send Requests if current and desired equal");
        make_request = false;
    }
    // </3T:TommyTheTerrible>
    else if (gTextureList.aDecodingCount >= 512 || LLAppViewer::instance()->getImageDecodeThread()->getPending() >= 512)
    {
        make_request = false;
    }

    if (make_request)
    {
        if (mIsFetching)
        {
            /*
            // already requested a higher resolution mip
            if (mRequestedDiscardLevel <= desired_discard)
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - requested < desired");
                make_request = false;
            }
            */

            if (LLAppViewer::getTextureFetch() && decode_priority > 0)
            {
                LLAppViewer::getTextureFetch()->updateRequestPriority(mID, decode_priority);
            }

            make_request = false;
        }
        else
        {
            // already at a higher resolution mip, don't discard
            if (current_discard >= 0 && current_discard == desired_discard) // <TS:3T> Stop expecting all new discards to always be lower
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - current == desired");
                make_request = false;
            }
        }
    }

    if (make_request)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - make request");
        S32 w=0, h=0, c=0;
        if (getDiscardLevel() >= 0)
        {
            w = mGLTexturep->getWidth(0);
            h = mGLTexturep->getHeight(0);
            c = mComponents;
        }
        // bypass texturefetch directly by pulling from LLTextureCache
        S32 fetch_request_discard = -1;
        fetch_request_discard = LLAppViewer::getTextureFetch()->createRequest(mFTType, mUrl, getID(), getTargetHost(), decode_priority, w,
                                                                              h, c, desired_discard, needsAux(), mCanUseHTTP);
        if (fetch_request_discard == -1)
        {
            LL_WARNS_ONCE() << "fetchRequest: " << mID << " " << (S32)getType() << " wXh " << w << " x " << h
                            << " Current: " << current_discard << " Current Size: " << mGLTexturep->getWidth(current_discard) << " x "
                            << mGLTexturep->getHeight(current_discard) << " previous: " << (S32)mRequestedDiscardLevel
                            << " Desired: " << desired_discard << " mTextureState: " << (S32)mTextureState
                            << " needsAux(): " << (S32)needsAux() << " getFTType(): " << mFTType << " forSculpt(): " << forSculpt()
                            << " mForceToSaveRawImage: " << mForceToSaveRawImage << " mSavedRawDiscardLevel: " << mSavedRawDiscardLevel
                            << " mBoostLevel: " << mBoostLevel << " mMaxVirtualSize:" << (S32)mMaxVirtualSize
                            << " fetch_request_discard: " << (S32)fetch_request_discard
                            << " sDesiredDiscardBias: " << LLViewerTexture::sDesiredDiscardBias << " DontDiscard: " << (S32)getDontDiscard()
                            << LL_ENDL;
        }
        mLastFetchState = fetch_request_discard;
        if (fetch_request_discard >= 0)
        {
            mLastUpdateFrame = LLViewerOctreeEntryData::getCurrentFrame();
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - request created");
            mHasFetcher = true;
            mIsFetching = true;
            // in some cases createRequest can modify discard, as an example
            // bake textures are always at discard 0
            mRequestedDiscardLevel = llmin(desired_discard, fetch_request_discard);
            mFetchState = LLAppViewer::getTextureFetch()->getFetchState(mID, mDownloadProgress, mRequestedDownloadPriority, mFetchPriority,
                                                                        mFetchDeltaTime, mRequestDeltaTime, mCanUseHTTP);
        }

        // If createRequest() failed, that means one of two things:
        // 1. We're finishing up a request for this UUID, so we
        //    should wait for it to complete
        // 2. We've failed a request for this UUID, so there is
        //    no need to create another request
    }
    else if (mHasFetcher && !mIsFetching)
    {
        // Only delete requests that haven't received any network data
        // for a while.  Note - this is the normal mechanism for
        // deleting requests, not just a place to handle timeouts.
        const F32 FETCH_IDLE_TIME = 0.1f;
        //<3T:TommyTheTerrible> Do not delete job if decoding or writing to cache.
        // if (mLastPacketTimer.getElapsedTimeF32() > FETCH_IDLE_TIME)
        if (mLastPacketTimer.getElapsedTimeF32() > FETCH_IDLE_TIME && (mFetchState < 10 || mFetchState == 14))
        //</3T>
        {
            LL_DEBUGS("Texture") << "exceeded idle time " << FETCH_IDLE_TIME << ", deleting request: " << getID() << LL_ENDL;
            LLAppViewer::getTextureFetch()->deleteRequest(getID(), true);
            mHasFetcher = false;
            mLastTimeUpdated.reset();
        }
    }

    if (mIsFetching && make_request)
        gTextureList.mFetchingTextures.insert(this);
    else if (!mHasFetcher)
    {
        LLViewerFetchedTexture& unref = *this; // Need to do this to reduce the references on the texture.
        gTextureList.mFetchingTextures.erase(this);
    }

    return mIsFetching;
}

void LLViewerFetchedTexture::clearFetchedResults()
{
    // <FS:Ansariel> For texture refresh
    mIsMissingAsset = false;

    if(mNeedsCreateTexture || mIsFetching)
    {
        return;
    }

    cleanup();
    destroyGLTexture();

    if(getDiscardLevel() >= 0) //sculpty texture, force to invalidate
    {
        mGLTexturep->forceToInvalidateGLTexture();
    }
}

void LLViewerFetchedTexture::forceToDeleteRequest()
{
    if (mHasFetcher)
    {
        mHasFetcher = false;
        mIsFetching = false;
    }

    resetTextureStats();

    mDesiredDiscardLevel = getMaxDiscardLevel() + 1;
}

void LLViewerFetchedTexture::setIsMissingAsset(bool is_missing)
{
    if (is_missing == mIsMissingAsset)
    {
        return;
    }
    if (is_missing)
    {
        if (mUrl.empty())
        {
            LL_WARNS() << mID << ": Marking image as missing" << LL_ENDL;
        }
        else
        {
            // This may or may not be an error - it is normal to have no
            // map tile on an empty region, but bad if we're failing on a
            // server bake texture.
            if (getFTType() != FTT_MAP_TILE)
            {
                LL_WARNS() << mUrl << ": Marking image as missing" << LL_ENDL;
            }
        }
        if (mHasFetcher)
        {
            LLAppViewer::getTextureFetch()->deleteRequest(getID(), true);
            mHasFetcher = false;
            mIsFetching = false;
            mLastPacketTimer.reset();
            mFetchState = 0;
            mFetchPriority = 0;
        }
    }
    else
    {
        LL_INFOS() << mID << ": un-flagging missing asset" << LL_ENDL;
    }
    mIsMissingAsset = is_missing;
}

void LLViewerFetchedTexture::setLoadedCallback( loaded_callback_func loaded_callback,
                                       S32 discard_level, bool keep_imageraw, bool needs_aux, void* userdata,
                                       LLLoadedCallbackEntry::source_callback_list_t* src_callback_list, bool pause)
{
    //
    // Don't do ANYTHING here, just add it to the global callback list
    //
    if (mLoadedCallbackList.empty())
    {
        // Put in list to call this->doLoadedCallbacks() periodically
        gTextureList.mCallbackList.insert(this);
        mLoadedCallbackDesiredDiscardLevel = (S8)discard_level;
    }
    // <TS:3T> Stop expecting all new discards to always be lower
    //else
    //{
    //    mLoadedCallbackDesiredDiscardLevel = llmin(mLoadedCallbackDesiredDiscardLevel, (S8)discard_level); 
    //}
    // </TS:3T>

    if(mPauseLoadedCallBacks)
    {
        if(!pause)
        {
            unpauseLoadedCallbacks(src_callback_list);
        }
    }
    else if(pause)
    {
        pauseLoadedCallbacks(src_callback_list);
    }

    LLLoadedCallbackEntry* entryp = new LLLoadedCallbackEntry(loaded_callback, discard_level, keep_imageraw, userdata, src_callback_list, this, pause);
    mLoadedCallbackList.push_back(entryp);

    mNeedsAux |= needs_aux;
    if(keep_imageraw)
    {
        mSaveRawImage = true;
    }
    if (mNeedsAux && mAuxRawImage.isNull() && getDiscardLevel() >= 0)
    {
        if(mHasAux)
        {
            //trigger a refetch
            forceToRefetchTexture();
        }
        else
        {
            // We need aux data, but we've already loaded the image, and it didn't have any
            LL_WARNS() << "No aux data available for callback for image:" << getID() << LL_ENDL;
        }
    }
    mLastCallBackActiveTime = sCurrentTime ;
        mLastReferencedSavedRawImageTime = sCurrentTime;
}

void LLViewerFetchedTexture::clearCallbackEntryList()
{
    if(mLoadedCallbackList.empty())
    {
        return;
    }

    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter;

        // We never finished loading the image.  Indicate failure.
        // Note: this allows mLoadedCallbackUserData to be cleaned up.
        entryp->mCallback(false, this, NULL, NULL, 0, true, entryp->mUserData);
        iter = mLoadedCallbackList.erase(iter);
        delete entryp;
    }
    gTextureList.mCallbackList.erase(this);

    mLoadedCallbackDesiredDiscardLevel = S8_MAX;
    if(needsToSaveRawImage())
    {
        destroySavedRawImage();
    }

    return;
}

void LLViewerFetchedTexture::deleteCallbackEntry(const LLLoadedCallbackEntry::source_callback_list_t* callback_list)
{
    if(mLoadedCallbackList.empty() || !callback_list)
    {
        return;
    }

    S32 desired_discard = S8_MAX;
    S32 desired_raw_discard = INVALID_DISCARD_LEVEL;
    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter;
        if(entryp->mSourceCallbackList == callback_list)
        {
            // We never finished loading the image.  Indicate failure.
            // Note: this allows mLoadedCallbackUserData to be cleaned up.
            entryp->mCallback(false, this, NULL, NULL, 0, true, entryp->mUserData);
            iter = mLoadedCallbackList.erase(iter);
            delete entryp;
        }
        else
        {
            ++iter;

            desired_discard = llmin(desired_discard, entryp->mDesiredDiscard);
            if(entryp->mNeedsImageRaw)
            {
                desired_raw_discard = llmin(desired_raw_discard, entryp->mDesiredDiscard);
            }
        }
    }

    mLoadedCallbackDesiredDiscardLevel = desired_discard;
    if (mLoadedCallbackList.empty())
    {
        // If we have no callbacks, take us off of the image callback list.
        gTextureList.mCallbackList.erase(this);

        if(needsToSaveRawImage())
        {
            destroySavedRawImage();
        }
    }
    else if(needsToSaveRawImage() && mBoostLevel != LLGLTexture::BOOST_PREVIEW)
    {
        if(desired_raw_discard != INVALID_DISCARD_LEVEL)
        {
            mDesiredSavedRawDiscardLevel = desired_raw_discard;
        }
        else
        {
            destroySavedRawImage();
        }
    }
}

void LLViewerFetchedTexture::unpauseLoadedCallbacks(const LLLoadedCallbackEntry::source_callback_list_t* callback_list)
{
    if(!callback_list)
{
        mPauseLoadedCallBacks = false;
        return;
    }

    bool need_raw = false;
    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter++;
        if(entryp->mSourceCallbackList == callback_list)
        {
            entryp->mPaused = false;
            if(entryp->mNeedsImageRaw)
            {
                need_raw = true;
            }
        }
    }
    mPauseLoadedCallBacks = false ;
    mLastCallBackActiveTime = sCurrentTime ;
    mForceCallbackFetch = true;
    if(need_raw)
    {
        mSaveRawImage = true;
    }
}

void LLViewerFetchedTexture::pauseLoadedCallbacks(const LLLoadedCallbackEntry::source_callback_list_t* callback_list)
{
    if(!callback_list)
{
        return;
    }

    bool paused = true;

    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter++;
        if(entryp->mSourceCallbackList == callback_list)
        {
            entryp->mPaused = true;
        }
        else if(!entryp->mPaused)
        {
            paused = false;
        }
    }

    if(paused)
    {
        mPauseLoadedCallBacks = true;//when set, loaded callback is paused.
        resetTextureStats();
        mSaveRawImage = false;
    }
}

bool LLViewerFetchedTexture::doLoadedCallbacks()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static const F32 MAX_INACTIVE_TIME = 900.f ; //seconds
    static const F32 MAX_IDLE_WAIT_TIME = 5.f ; //seconds

    if (mNeedsCreateTexture)
    {
        return false;
    }
    if(mPauseLoadedCallBacks)
    {
        destroyRawImage();
        return false; //paused
    }
    if(sCurrentTime - mLastCallBackActiveTime > MAX_INACTIVE_TIME && !mIsFetching)
    {
        if (mFTType == FTT_SERVER_BAKE)
        {
            //output some debug info
            LL_INFOS() << "baked texture: " << mID << "clears all call backs due to inactivity." << LL_ENDL;
            LL_INFOS() << mUrl << LL_ENDL;
            LL_INFOS() << "current discard: " << getDiscardLevel() << " current discard for fetch: " << getCurrentDiscardLevelForFetching() <<
                " Desired discard: " << getDesiredDiscardLevel() << "decode Pri: " << mMaxVirtualSize << LL_ENDL;
        }

        clearCallbackEntryList() ; //remove all callbacks.
        return false ;
    }

    bool res = false;

    if (isMissingAsset())
    {
        if (mFTType == FTT_SERVER_BAKE)
        {
            //output some debug info
            LL_INFOS() << "baked texture: " << mID << "is missing." << LL_ENDL;
            LL_INFOS() << mUrl << LL_ENDL;
        }

        for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
        {
            LLLoadedCallbackEntry *entryp = *iter++;
            // We never finished loading the image.  Indicate failure.
            // Note: this allows mLoadedCallbackUserData to be cleaned up.
            entryp->mCallback(false, this, NULL, NULL, 0, true, entryp->mUserData);
            delete entryp;
        }
        mLoadedCallbackList.clear();

        // Remove ourself from the global list of textures with callbacks
        gTextureList.mCallbackList.erase(this);
        return false;
    }

    S32 gl_discard = getDiscardLevel();

    // If we don't have a legit GL image, set it to be lower than the worst discard level
    if (gl_discard == -1)
    {
        gl_discard = MAX_DISCARD_LEVEL;
    }

    //
    // Determine the quality levels of textures that we can provide to callbacks
    // and whether we need to do decompression/readback to get it
    //
    //S32 current_raw_discard = MAX_DISCARD_LEVEL; // We can always do a readback to get a raw discard
    S32 best_raw_discard = gl_discard;  // Current GL quality level
    S32 current_aux_discard = MAX_DISCARD_LEVEL;
    S32 best_aux_discard = best_raw_discard;

    if (mIsRawImageValid)
    {
        // If we have an existing raw image, we have a baseline for the raw and auxiliary quality levels.
        //current_raw_discard = mRawDiscardLevel;
        best_raw_discard = llmin(best_raw_discard, mRawDiscardLevel);
        best_aux_discard = llmin(best_aux_discard, mRawDiscardLevel); // We always decode the aux when we decode the base raw
        current_aux_discard = llmin(current_aux_discard, best_aux_discard);
    }
    //else
    //{
    //    // We have no data at all, we need to get it
    //    // Do this by forcing the best aux discard to be 0.
    //    best_aux_discard = 0;
    //}


    //
    // See if any of the callbacks would actually run using the data that we can provide,
    // and also determine if we need to perform any readbacks or decodes.
    //
    bool run_gl_callbacks = false;
    bool run_raw_callbacks = false;
    bool need_readback = false;

    for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
        iter != mLoadedCallbackList.end(); )
    {
        LLLoadedCallbackEntry *entryp = *iter++;

        if (entryp->mNeedsImageRaw)
        {
            if (mNeedsAux)
            {
                //
                // Need raw and auxiliary channels
                //
                if (entryp->mLastUsedDiscard != current_aux_discard)
                {
                    // We have useful data, run the callbacks
                    run_raw_callbacks = true;
                }
            }
            else
            {
                if (entryp->mLastUsedDiscard != gl_discard)
                {
                    // We have useful data, just run the callbacks
                    run_raw_callbacks = true;
                }
                //else
                //if (entryp->mLastUsedDiscard != best_raw_discard)  //<TS:3T> Stop expecting all new discards to be lower
                //{
                //    // We can readback data, and then run the callbacks
                //    need_readback = true;
                //    run_raw_callbacks = true;
                //}
            }
        }
        else
        {
            // Needs just GL
            if (entryp->mLastUsedDiscard != gl_discard)  //<TS:3T> Stop expecting all new discards to be lower
            {
                // We have enough data, run this callback requiring GL data
                run_gl_callbacks = true;
            }
        }
    }

    if (need_readback)
    {
        readbackRawImage();
    }

    //
    // Run raw/auxiliary data callbacks
    //
    if (run_raw_callbacks && mIsRawImageValid && (mRawDiscardLevel <= getMaxDiscardLevel()))
    {
        // Do callbacks which require raw image data.
        //LL_INFOS() << "doLoadedCallbacks raw for " << getID() << LL_ENDL;

        // Call each party interested in the raw data.
        for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
        {
            callback_list_t::iterator curiter = iter++;
            LLLoadedCallbackEntry *entryp = *curiter;
            if (entryp->mNeedsImageRaw && (entryp->mLastUsedDiscard != mRawDiscardLevel)) //<TS:3T> Stop expecting all new discards to be lower
            {
                // If we've loaded all the data there is to load or we've loaded enough
                // to satisfy the interested party, then this is the last time that
                // we're going to call them.

                mLastCallBackActiveTime = sCurrentTime;
                if(mNeedsAux && mAuxRawImage.isNull())
                {
                    LL_WARNS() << "Raw Image with no Aux Data for callback" << LL_ENDL;
                }
                bool final = (mRawDiscardLevel == entryp->mDesiredDiscard); //<TS:3T> Stop expecting all new discards to be lower
                //LL_INFOS() << "Running callback for " << getID() << LL_ENDL;
                //LL_INFOS() << mRawImage->getWidth() << "x" << mRawImage->getHeight() << LL_ENDL;
                entryp->mLastUsedDiscard = mRawDiscardLevel;
                entryp->mCallback(true, this, mRawImage, mAuxRawImage, mRawDiscardLevel, final, entryp->mUserData);
                if (final)
                {
                    iter = mLoadedCallbackList.erase(curiter);
                    delete entryp;
                }
                res = true;
            }
        }
    }

    //
    // Run GL callbacks
    //
    if (run_gl_callbacks && (gl_discard <= getMaxDiscardLevel()))
    {
        //LL_INFOS() << "doLoadedCallbacks GL for " << getID() << LL_ENDL;

        // Call the callbacks interested in GL data.
        for(callback_list_t::iterator iter = mLoadedCallbackList.begin();
            iter != mLoadedCallbackList.end(); )
        {
            callback_list_t::iterator curiter = iter++;
            LLLoadedCallbackEntry *entryp = *curiter;
            if (!entryp->mNeedsImageRaw && (entryp->mLastUsedDiscard > gl_discard))
            {
                mLastCallBackActiveTime = sCurrentTime;
                bool final = gl_discard <= entryp->mDesiredDiscard;
                entryp->mLastUsedDiscard = gl_discard;
                entryp->mCallback(true, this, NULL, NULL, gl_discard, final, entryp->mUserData);
                if (final)
                {
                    iter = mLoadedCallbackList.erase(curiter);
                    delete entryp;
                }
                res = true;
            }
        }
    }

    // Done with any raw image data at this point (will be re-created if we still have callbacks)
    destroyRawImage();

    //
    // If we have no callbacks, take us off of the image callback list.
    //
    if (mLoadedCallbackList.empty())
    {
        gTextureList.mCallbackList.erase(this);
    }
    else if(!res && mForceCallbackFetch && sCurrentTime - mLastCallBackActiveTime > MAX_IDLE_WAIT_TIME && !mIsFetching)
    {
        //wait for long enough but no fetching request issued, force one.
        forceToRefetchTexture(mLoadedCallbackDesiredDiscardLevel, 5.f);
        mForceCallbackFetch = false; //fire once.
    }

    return res;
}

//virtual
void LLViewerFetchedTexture::forceImmediateUpdate()
{
    //only immediately update a deleted texture which is now being re-used.
    if(!isDeleted())
    {
        return;
    }
    //if already called forceImmediateUpdate()
    if(mInImageList && mMaxVirtualSize == LLViewerFetchedTexture::sMaxVirtualSize)
    {
        return;
    }

    gTextureList.forceImmediateUpdate(this);
    return;
}

//LLImageRaw* LLViewerFetchedTexture::reloadRawImage(S8 discard_level)
//{
//    llassert(mGLTexturep.notNull());
//    llassert(discard_level >= 0);
//    llassert(mComponents > 0);
//
//    if (mRawImage.notNull())
//    {
//        //mRawImage is in use by somebody else, do not delete it.
//        return NULL;
//    }
//
//    if(mSavedRawDiscardLevel >= 0 && mSavedRawDiscardLevel != discard_level) // <TS:3T> Stop expecting the discard level to always be lower
//    {
//        if (mSavedRawDiscardLevel != discard_level
//            && mBoostLevel != BOOST_ICON
//            && mBoostLevel != BOOST_THUMBNAIL)
//        {
//            mRawImage = new LLImageRaw(getWidth(discard_level), getHeight(discard_level), getComponents());
//            mRawImage->copy(getSavedRawImage());
//        }
//        else
//        {
//            mRawImage = getSavedRawImage();
//        }
//        mRawDiscardLevel = discard_level;
//    }
//    //else
//    //{
//    //    //force to fetch raw image again if cached raw image is not good enough.
//    //    if(mCachedRawDiscardLevel > discard_level)
//    //    {
//    //        mRawImage = mCachedRawImage;
//    //        mRawDiscardLevel = mCachedRawDiscardLevel;
//    //    }
//    //    else //cached raw image is good enough, copy it.
//    //    {
//    //        if(mCachedRawDiscardLevel != discard_level)
//    //        {
//    //            mRawImage = new LLImageRaw(getWidth(discard_level), getHeight(discard_level), getComponents());
//    //            mRawImage->copy(mCachedRawImage);
//    //        }
//    //        else
//    //        {
//    //            mRawImage = mCachedRawImage;
//    //        }
//    //        mRawDiscardLevel = discard_level;
//    //    }
//    //}
//    mIsRawImageValid = TRUE;
//    sRawCount++;
//
//    return mRawImage;
//}

bool LLViewerFetchedTexture::needsToSaveRawImage()
{
    return mForceToSaveRawImage || mSaveRawImage;
}

void LLViewerFetchedTexture::destroyRawImage()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mAuxRawImage.notNull() && !needsToSaveRawImage())
    {
        sAuxCount--;
        mAuxRawImage = nullptr;
    }

    if (mRawImage.notNull())
    {
        sRawCount--;

        if(mIsRawImageValid)
        {
            if(needsToSaveRawImage())
            {
                saveRawImage();
            }
        }

        mRawImage = nullptr;

        mIsRawImageValid = false;
        mRawDiscardLevel = INVALID_DISCARD_LEVEL;
    }
}

void LLViewerFetchedTexture::saveRawImage()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if(mRawImage.isNull() || mRawImage == mSavedRawImage || (mSavedRawDiscardLevel >= 0 && mSavedRawDiscardLevel <= mRawDiscardLevel))
    {
        return;
    }

    LLImageDataSharedLock lock(mRawImage);

    mSavedRawDiscardLevel = mRawDiscardLevel;
    if (mBoostLevel == LLGLTexture::BOOST_ICON)
    {
        S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_ICON_DIMENSIONS;
        S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_ICON_DIMENSIONS;
        if (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height)
        {
            mSavedRawImage = new LLImageRaw(expected_width, expected_height, mRawImage->getComponents());
            mSavedRawImage->copyScaled(mRawImage);
        }
        else
        {
            mSavedRawImage = new LLImageRaw(mRawImage->getData(), mRawImage->getWidth(), mRawImage->getHeight(), mRawImage->getComponents());
        }
    }
    else if (mBoostLevel == LLGLTexture::BOOST_THUMBNAIL)
    {
        S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : DEFAULT_THUMBNAIL_DIMENSIONS;
        S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : DEFAULT_THUMBNAIL_DIMENSIONS;
        if (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height)
        {
            mSavedRawImage = new LLImageRaw(expected_width, expected_height, mRawImage->getComponents());
            mSavedRawImage->copyScaled(mRawImage);
        }
        else
        {
            mSavedRawImage = new LLImageRaw(mRawImage->getData(), mRawImage->getWidth(), mRawImage->getHeight(), mRawImage->getComponents());
        }
    }
    else if (mBoostLevel == LLGLTexture::BOOST_SCULPTED)
    {
        S32 expected_width = mKnownDrawWidth > 0 ? mKnownDrawWidth : sMaxSculptRez;
        S32 expected_height = mKnownDrawHeight > 0 ? mKnownDrawHeight : sMaxSculptRez;
        if (mRawImage->getWidth() > expected_width || mRawImage->getHeight() > expected_height)
        {
            mSavedRawImage = new LLImageRaw(expected_width, expected_height, mRawImage->getComponents());
            mSavedRawImage->copyScaled(mRawImage);
        }
        else
        {
            mSavedRawImage = new LLImageRaw(mRawImage->getData(), mRawImage->getWidth(), mRawImage->getHeight(), mRawImage->getComponents());
        }
    }
    else
    {
        mSavedRawImage = new LLImageRaw(mRawImage->getData(), mRawImage->getWidth(), mRawImage->getHeight(), mRawImage->getComponents());
    }

    if(mForceToSaveRawImage && mSavedRawDiscardLevel <= mDesiredSavedRawDiscardLevel)
    {
        mForceToSaveRawImage = false;
    }

    mLastReferencedSavedRawImageTime = sCurrentTime;
}

//force to refetch the texture to the discard level
void LLViewerFetchedTexture::forceToRefetchTexture(S32 desired_discard, F32 kept_time)
{
    if(mForceToSaveRawImage)
    {
        desired_discard = llmin(desired_discard, mDesiredSavedRawDiscardLevel);
        kept_time = llmax(kept_time, mKeptSavedRawImageTime);
    }

    //trigger a new fetch.
    //mForceToSaveRawImage = TRUE ;
    mDesiredSavedRawDiscardLevel = desired_discard ;
    mKeptSavedRawImageTime = kept_time ;
    mLastReferencedSavedRawImageTime = sCurrentTime ;
    mSavedRawImage = NULL ;
    mSavedRawDiscardLevel = -1 ;
}

void LLViewerFetchedTexture::forceToSaveRawImage(S32 desired_discard, F32 kept_time)
{
    mKeptSavedRawImageTime = kept_time;
    mLastReferencedSavedRawImageTime = sCurrentTime;

    if(mSavedRawDiscardLevel > -1 && mSavedRawDiscardLevel <= desired_discard)
    {
        return; //raw imge is ready.
    }

    if(!mForceToSaveRawImage || mDesiredSavedRawDiscardLevel < 0 || mDesiredSavedRawDiscardLevel > desired_discard)
    {
        mForceToSaveRawImage = true;
        mDesiredSavedRawDiscardLevel = desired_discard;
    }
}

void LLViewerFetchedTexture::readbackRawImage()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    // readback the raw image from vram if the current raw image is null or smaller than the texture
    if (mGLTexturep.notNull() && mGLTexturep->getTexName() != 0 &&
        (mRawImage.isNull() || mRawImage->getWidth() < mGLTexturep->getWidth() || mRawImage->getHeight() < mGLTexturep->getHeight() ))
    {
        if (mRawImage.isNull())
        {
            sRawCount++;
        }
        mRawImage = new LLImageRaw();
        if (!mGLTexturep->readBackRaw(-1, mRawImage, false))
        {
            mRawImage = nullptr;
            mIsRawImageValid = false;
            mRawDiscardLevel = INVALID_DISCARD_LEVEL;
            sRawCount--;
        }
        else
        {
            mIsRawImageValid = true;
            mRawDiscardLevel = mGLTexturep->getDiscardLevel();
        }
    }
}

void LLViewerFetchedTexture::destroySavedRawImage()
{
    if(mLastReferencedSavedRawImageTime < mKeptSavedRawImageTime)
    {
        return; //keep the saved raw image.
    }

    mForceToSaveRawImage  = false;
    mSaveRawImage = false;

    clearCallbackEntryList();

    mSavedRawImage = NULL ;
    mForceToSaveRawImage  = false ;
    mSaveRawImage = false ;
    mSavedRawDiscardLevel = -1 ;
    mDesiredSavedRawDiscardLevel = -1 ;
    mLastReferencedSavedRawImageTime = 0.0f ;
    mKeptSavedRawImageTime = 0.f ;

    if(mAuxRawImage.notNull())
    {
        sAuxCount--;
        mAuxRawImage = NULL;
    }
}

LLImageRaw* LLViewerFetchedTexture::getSavedRawImage()
{
    mLastReferencedSavedRawImageTime = sCurrentTime;

    return mSavedRawImage;
}

const LLImageRaw* LLViewerFetchedTexture::getSavedRawImage() const
{
    return mSavedRawImage;
}

bool LLViewerFetchedTexture::hasSavedRawImage() const
{
    return mSavedRawImage.notNull();
}

F32 LLViewerFetchedTexture::getElapsedLastReferencedSavedRawImageTime() const
{
    return sCurrentTime - mLastReferencedSavedRawImageTime;
}

//----------------------------------------------------------------------------------------------
//end of LLViewerFetchedTexture
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
//start of LLViewerLODTexture
//----------------------------------------------------------------------------------------------
LLViewerLODTexture::LLViewerLODTexture(const LLUUID& id, FTType f_type, const LLHost& host, bool usemipmaps)
    : LLViewerFetchedTexture(id, f_type, host, usemipmaps)
{
    init(true);
}

LLViewerLODTexture::LLViewerLODTexture(const std::string& url, FTType f_type, const LLUUID& id, bool usemipmaps)
    : LLViewerFetchedTexture(url, f_type, id, usemipmaps)
{
    init(true);
}

void LLViewerLODTexture::init(bool firstinit)
{
    mTexelsPerImage = 64*64;
    mLastUpdateFrame = 0; // <FS:3T> Tracking last frame a texture was updated.
}

//virtual
S8 LLViewerLODTexture::getType() const
{
    return LLViewerTexture::LOD_TEXTURE;
}

bool LLViewerLODTexture::isUpdateFrozen()
{
    return LLViewerTexture::sFreezeImageUpdates;
}

// This is gauranteed to get called periodically for every texture
//virtual
void LLViewerLODTexture::processTextureStats()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    updateVirtualSize();

    bool did_downscale = false;

    static LLCachedControl<bool> textures_fullres(gSavedSettings,"TextureLoadFullRes", false);

    F32 max_tex_res = MAX_IMAGE_SIZE_DEFAULT;
    if (mBoostLevel < LLGLTexture::BOOST_HIGH)
    {
        // restrict texture resolution to download based on RenderMaxTextureResolution
        static LLCachedControl<U32> max_texture_resolution(gSavedSettings, "RenderMaxTextureResolution", 2048);
        // sanity clamp debug setting to avoid settings hack shenanigans
        max_tex_res = (F32)llclamp((S32)max_texture_resolution, 512, MAX_IMAGE_SIZE_DEFAULT);
        mMaxVirtualSize = llmin(mMaxVirtualSize, max_tex_res * max_tex_res);
    }

    if (textures_fullres)
    {
        mDesiredDiscardLevel = 0;
    }
    // Generate the request priority and render priority
    else if (mDontDiscard || !mUseMipMaps)
    {
        mDesiredDiscardLevel = 0;
        if (mFullWidth > MAX_IMAGE_SIZE_DEFAULT || mFullHeight > MAX_IMAGE_SIZE_DEFAULT)
            mDesiredDiscardLevel = 1; // MAX_IMAGE_SIZE_DEFAULT = 2048 and max size ever is 4096
    }
    else if (mBoostLevel < LLGLTexture::BOOST_HIGH && mMaxVirtualSize <= 10.f)
    {
        // If the image has not been significantly visible in a while, we don't want it
        // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
        //mDesiredDiscardLevel = llmin(mMinDesiredDiscardLevel, (S8)(MAX_DISCARD_LEVEL + 1));
        // Off screen textures at 6 would not downscale.
        mDesiredDiscardLevel = llmin(mMinDesiredDiscardLevel, (S8)(MAX_DISCARD_LEVEL));
        // </FS:minerjr> [FIRE-35081]
        //mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S32)mLoadedCallbackDesiredDiscardLevel);
        /* <TommyTheTerrible> Unnecessary component of older brute force texture system.
        // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings, not happening with SL Viewer
        // Add scale down here as the textures off screen were not getting scaled down properly
        S32 current_discard = getDiscardLevel();
        if (mBoostLevel < LLGLTexture::BOOST_AVATAR_BAKED)
        {
            if (current_discard < mDesiredDiscardLevel && !mForceToSaveRawImage)
            { // should scale down
                scaleDown();
            }
        }
        // </FS:minerjr> [FIRE-35081]
        */
    }
    else if (!mFullWidth  || !mFullHeight)
    {
        mDesiredDiscardLevel =  getMaxDiscardLevel();
        //mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S32)mLoadedCallbackDesiredDiscardLevel);
    }
    else
    {
        // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings, not happening with SL Viewer
        
        //static const F64 log_2 = log(2.0);
        static const F64 log_4 = log(4.0);

        F32 discard_level = 0.f;

        // If we know the output width and height, we can force the discard
        // level to the correct value, and thus not decode more texture
        // data than we need to.
        if (mKnownDrawWidth && mKnownDrawHeight)
        {
            S32 draw_texels = mKnownDrawWidth * mKnownDrawHeight;
            draw_texels = llclamp(draw_texels, MIN_IMAGE_AREA, MAX_IMAGE_AREA);

            // Use log_4 because we're in square-pixel space, so an image
            // with twice the width and twice the height will have mTexelsPerImage
            // 4 * draw_size
            discard_level = (F32)(log(mTexelsPerImage / draw_texels) / log_4);
        }
        else
        {
            // Calculate the required scale factor of the image using pixels per texel
            discard_level = (F32)(log(mTexelsPerImage / mMaxVirtualSize) / log_4);
        }

        discard_level = floorf(discard_level);

        //F32 min_discard = 0.f;
        
        // Use a S32 value for the discard level
        //S32 discard_level = 0;
        // Find the best discard that covers the entire mMaxVirtualSize of the on screen texture
        //for (; discard_level <= MAX_DISCARD_LEVEL; discard_level++)
        //{
            // If the max virtual size is greater then the current discard level, then break out of the loop and use the current discard level
         //   if (mMaxVirtualSize > getWidth(discard_level) * getHeight(discard_level))
        //    {
        //        break;
        //   }
        //}


        //discard_level = llclamp(discard_level, min_discard, (F32)MAX_DISCARD_LEVEL);
        // </FS:minerjr> [FIRE-35081]

        // Can't go higher than the max discard level
        mDesiredDiscardLevel = llmin(getMaxDiscardLevel(), (S32)discard_level);
        // Clamp to min desired discard
        mDesiredDiscardLevel = llmin(mMinDesiredDiscardLevel, mDesiredDiscardLevel);

        if (mBoostLevel == LLGLTexture::BOOST_SCULPTED)
            mDesiredDiscardLevel = 0;
        //
        // At this point we've calculated the quality level that we want,
        // if possible.  Now we check to see if we have it, and take the
        // proper action if we don't.
        //

        S32 current_discard = getDiscardLevel();
        // <TS:3T> We do not do this anymore, instead allow a new decode when increasing discard.
        //if (mBoostLevel < LLGLTexture::BOOST_AVATAR_BAKED &&
        //    current_discard >= 0)
        //{
        //    if (current_discard < (mDesiredDiscardLevel-1) && !mForceToSaveRawImage)
        //    { // should scale down
        //        scaleDown();
        //    }
        //}
        // </TS:3T>

        if (isUpdateFrozen() // we are out of memory and nearing max allowed bias
            && mBoostLevel < LLGLTexture::BOOST_SCULPTED
            && mDesiredDiscardLevel < current_discard)
        {
            // stop requesting more
            mDesiredDiscardLevel = current_discard;
        }
        mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S32)mLoadedCallbackDesiredDiscardLevel);
    }

    if(mForceToSaveRawImage && mDesiredSavedRawDiscardLevel >= 0)
    {
        mDesiredDiscardLevel = llmin(mDesiredDiscardLevel, (S8)mDesiredSavedRawDiscardLevel);
    }

    // decay max virtual size over time
    //mMaxVirtualSize *= 0.8f; <TS:3T> We should not do this, allow true values to stay in queue.

    // selection manager will immediately reset BOOST_SELECTED but never unsets it
    // unset it immediately after we consume it
    if (getBoostLevel() == BOOST_SELECTED)
    {
        // <FS:minerjr>
        //setBoostLevel(BOOST_NONE);
        restoreBoostLevel();
        // </FS:minerjr>
    }
    //<3T:TommyTheTerrible> Do not allow desired discard below 0.
    mDesiredDiscardLevel = llmax(mDesiredDiscardLevel, 0);
    //</3T>

}

extern LLGLSLShader gCopyProgram;

bool LLViewerLODTexture::scaleDown()
{
    if (mGLTexturep.isNull() || !mGLTexturep->getHasGLTexture())
    {
        return false;
    }

    if (!mDownScalePending)
    {
        mDownScalePending = true;
        gTextureList.mDownScaleQueue.push(this);
    }

    return true;
}

//----------------------------------------------------------------------------------------------
//end of LLViewerLODTexture
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
//start of LLViewerMediaTexture
//----------------------------------------------------------------------------------------------
//static
void LLViewerMediaTexture::updateClass()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static const F32 MAX_INACTIVE_TIME = 30.f;

#if 0
    //force to play media.
    gSavedSettings.setBOOL("AudioStreamingMedia", true);
#endif

    for(media_map_t::iterator iter = sMediaMap.begin(); iter != sMediaMap.end(); )
    {
        LLViewerMediaTexture* mediap = iter->second;

        if(mediap->getNumRefs() == 1) //one reference by sMediaMap
        {
            //
            //Note: delay some time to delete the media textures to stop endlessly creating and immediately removing media texture.
            //
            if(mediap->getLastReferencedTimer()->getElapsedTimeF32() > MAX_INACTIVE_TIME)
            {
                media_map_t::iterator cur = iter++;
                sMediaMap.erase(cur);
                continue;
            }
        }
        ++iter;
    }
}

//static
void LLViewerMediaTexture::removeMediaImplFromTexture(const LLUUID& media_id)
{
    LLViewerMediaTexture* media_tex = findMediaTexture(media_id);
    if(media_tex)
    {
        media_tex->invalidateMediaImpl();
    }
}

//static
void LLViewerMediaTexture::cleanUpClass()
{
    sMediaMap.clear();
}

//static
LLViewerMediaTexture* LLViewerMediaTexture::findMediaTexture(const LLUUID& media_id)
{
    media_map_t::iterator iter = sMediaMap.find(media_id);
    if(iter == sMediaMap.end())
    {
        return NULL;
    }

    LLViewerMediaTexture* media_tex = iter->second;
    media_tex->setMediaImpl();
    media_tex->getLastReferencedTimer()->reset();

    return media_tex;
}

LLViewerMediaTexture::LLViewerMediaTexture(const LLUUID& id, bool usemipmaps, LLImageGL* gl_image)
    : LLViewerTexture(id, usemipmaps),
    mMediaImplp(NULL),
    mUpdateVirtualSizeTime(0)
{
    sMediaMap.insert(std::make_pair(id, this));

    mGLTexturep = gl_image;

    if(mGLTexturep.isNull())
    {
        generateGLTexture();
    }

    mGLTexturep->setAllowCompression(false);

    mGLTexturep->setNeedsAlphaAndPickMask(false);

    mIsPlaying = false;

    setMediaImpl();

    setCategory(LLGLTexture::MEDIA);

    LLViewerTexture* tex = gTextureList.findImage(mID, TEX_LIST_STANDARD);
    if(tex) //this media is a parcel media for tex.
    {
        tex->setParcelMedia(this);
    }
}

//virtual
LLViewerMediaTexture::~LLViewerMediaTexture()
{
    LLViewerTexture* tex = gTextureList.findImage(mID, TEX_LIST_STANDARD);
    if(tex) //this media is a parcel media for tex.
    {
        tex->setParcelMedia(NULL);
    }
}

void LLViewerMediaTexture::reinit(bool usemipmaps /* = true */)
{
    llassert(mGLTexturep.notNull());

    mUseMipMaps = usemipmaps;
    getLastReferencedTimer()->reset();
    mGLTexturep->setUseMipMaps(mUseMipMaps);
    mGLTexturep->setNeedsAlphaAndPickMask(false);
}

void LLViewerMediaTexture::setUseMipMaps(bool mipmap)
{
    mUseMipMaps = mipmap;

    if(mGLTexturep.notNull())
    {
        mGLTexturep->setUseMipMaps(mipmap);
    }
}

//virtual
S8 LLViewerMediaTexture::getType() const
{
    return LLViewerTexture::MEDIA_TEXTURE;
}

void LLViewerMediaTexture::invalidateMediaImpl()
{
    mMediaImplp = NULL;
}

void LLViewerMediaTexture::setMediaImpl()
{
    if(!mMediaImplp)
    {
        mMediaImplp = LLViewerMedia::getInstance()->getMediaImplFromTextureID(mID);
    }
}

//return true if all faces to reference to this media texture are found
//Note: mMediaFaceList is valid only for the current instant
//      because it does not check the face validity after the current frame.
bool LLViewerMediaTexture::findFaces()
{
    mMediaFaceList.clear();

    bool ret = true;

    LLViewerTexture* tex = gTextureList.findImage(mID, TEX_LIST_STANDARD);
    if(tex) //this media is a parcel media for tex.
    {
        for (U32 ch = 0; ch < LLRender::NUM_TEXTURE_CHANNELS; ++ch)
        {
            const ll_face_list_t* face_list = tex->getFaceList(ch);
            U32 end = tex->getNumFaces(ch);
        for(U32 i = 0; i < end; i++)
        {
            if ((*face_list)[i]->isMediaAllowed())
            {
                mMediaFaceList.push_back((*face_list)[i]);
            }
        }
    }
    }

    if(!mMediaImplp)
    {
        return true;
    }

    //for media on a face.
    const std::list< LLVOVolume* >* obj_list = mMediaImplp->getObjectList();
    std::list< LLVOVolume* >::const_iterator iter = obj_list->begin();
    for(; iter != obj_list->end(); ++iter)
    {
        LLVOVolume* obj = *iter;
        if (obj->isDead())
        {
            // Isn't supposed to happen, objects are supposed to detach
            // themselves on markDead()
            // If this happens, viewer is likely to crash
            llassert(0);
            LL_WARNS() << "Dead object in mMediaImplp's object list" << LL_ENDL;
            ret = false;
            continue;
        }

        if (obj->mDrawable.isNull() || obj->mDrawable->isDead())
        {
            ret = false;
            continue;
        }

        S32 face_id = -1;
        S32 num_faces = obj->mDrawable->getNumFaces();
        while((face_id = obj->getFaceIndexWithMediaImpl(mMediaImplp, face_id)) > -1 && face_id < num_faces)
        {
            LLFace* facep = obj->mDrawable->getFace(face_id);
            if(facep)
            {
                mMediaFaceList.push_back(facep);
            }
            else
            {
                ret = false;
            }
        }
    }

    return ret;
}

void LLViewerMediaTexture::initVirtualSize()
{
    if(mIsPlaying)
    {
        return;
    }
    // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
    // Add camera importance to the media textures as well
    static LLCachedControl<F32> texture_camera_boost(gSavedSettings, "TextureCameraBoost", 7.f);
    F32 vsize = 0.0f;
    // </FS:minerjr> [FIRE-35081]
    findFaces();
    for(std::list< LLFace* >::iterator iter = mMediaFaceList.begin(); iter!= mMediaFaceList.end(); ++iter)
    {
        // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
        //addTextureStats((*iter)->getVirtualSize());
        // Add camera importance to the media textures as well
        vsize = (*iter)->getVirtualSize();
        vsize = vsize + (vsize * (*iter)->getImportanceToCamera() * texture_camera_boost);
        // Apply second boost based upon if the texture is close to the camera (< 16.1 meters * draw distance multiplier)
        vsize = vsize + (vsize * (*iter)->getCloseToCamera() * texture_camera_boost);
        addTextureStats(vsize);
        // </FS:minerjr> [FIRE-35081]
    }
}

void LLViewerMediaTexture::addMediaToFace(LLFace* facep)
{
    if(facep)
    {
        facep->setHasMedia(true);
    }
    if(!mIsPlaying)
    {
        return; //no need to add the face because the media is not in playing.
    }

    switchTexture(LLRender::DIFFUSE_MAP, facep);
}

void LLViewerMediaTexture::removeMediaFromFace(LLFace* facep)
{
    if(!facep)
    {
        return;
    }
    facep->setHasMedia(false);

    if(!mIsPlaying)
    {
        return; //no need to remove the face because the media is not in playing.
    }

    mIsPlaying = false; //set to remove the media from the face.
    switchTexture(LLRender::DIFFUSE_MAP, facep);
    mIsPlaying = true; //set the flag back.

    if(getTotalNumFaces() < 1) //no face referencing to this media
    {
        stopPlaying();
    }
}

//virtual
void LLViewerMediaTexture::addFace(U32 ch, LLFace* facep)
{
    LLViewerTexture::addFace(ch, facep);

    const LLTextureEntry* te = facep->getTextureEntry();
    if(te && te->getID().notNull())
    {
        LLViewerTexture* tex = gTextureList.findImage(te->getID(), TEX_LIST_STANDARD);
        if(tex)
        {
// [SL:KB] - Patch: Render-TextureToggle (Catznip-5.2)
            // See LLViewerMediaTexture::removeFace()
            if (facep->isDefaultTexture(ch))
            {
                return;
            }
// [/SL:KB]

            // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
            // Try to set the boost level to MEDIA to try to force the media to high quality
            tex->setBoostLevel(LLViewerTexture::MEDIA);
            // </FS:minerjr> [FIRE-35081]
            mTextureList.push_back(tex);//increase the reference number by one for tex to avoid deleting it.
            return;
        }
    }

    //check if it is a parcel media
    if(facep->getTexture() && facep->getTexture() != this && facep->getTexture()->getID() == mID)
    {
        mTextureList.push_back(facep->getTexture()); //a parcel media.
        return;
    }

    if(te && te->getID().notNull()) //should have a texture
    {
        LL_WARNS_ONCE() << "The face's texture " << te->getID() << " is not valid. Face must have a valid texture before media texture." << LL_ENDL;
        // This might break the object, but it likely isn't a 'recoverable' situation.
        LLViewerFetchedTexture* tex = LLViewerTextureManager::getFetchedTexture(te->getID());
        mTextureList.push_back(tex);
    }
}

//virtual
//void LLViewerMediaTexture::removeFace(U32 ch, LLFace* facep)
// [SL:KB] - Patch: Render-TextureToggle (Catznip-5.2)
void LLViewerMediaTexture::removeFace(U32 channel, LLFace* facep)
// [/SL:KB]
{
// [SL:KB] - Patch: Render-TextureToggle (Catznip-5.2)
    LLViewerTexture::removeFace(channel, facep);
// [/SL:KB]
//  LLViewerTexture::removeFace(ch, facep);

    const LLTextureEntry* te = facep->getTextureEntry();
    if(te && te->getID().notNull())
    {
        LLViewerTexture* tex = gTextureList.findImage(te->getID(), TEX_LIST_STANDARD);
        if(tex)
        {
            for(std::list< LLPointer<LLViewerTexture> >::iterator iter = mTextureList.begin();
                iter != mTextureList.end(); ++iter)
            {
                if(*iter == tex)
                {
// [SL:KB] - Patch: Render-TextureToggle (Catznip-5.2)
                    // Switching to the default texture results in clearing the media textures on all prims;
                    // a side-effect is that we loose out on the reference to the original (non-media)
                    // texture potentially letting it dissapear from memory if this was the only reference to it
                    // (which is harmless, it just means we'll need to grab it from the cache or refetch it but
                    // the LL - debug - code at the bottom of addFace/removeFace disagrees so we'll hang on
                    // to it (and then block readding it a seond time higher up)
                    if (facep->isDefaultTexture(channel))
                    {
                        return;
                    }
// [/SL:KB]
                    mTextureList.erase(iter); //decrease the reference number for tex by one.
                    return;
                }
            }

            std::vector<const LLTextureEntry*> te_list;

            for (U32 ch = 0; ch < 3; ++ch)
            {
            //
            //we have some trouble here: the texture of the face is changed.
            //we need to find the former texture, and remove it from the list to avoid memory leaking.

                llassert(mNumFaces[ch] <= mFaceList[ch].size());

                for(U32 j = 0; j < mNumFaces[ch]; j++)
                {
                    te_list.push_back(mFaceList[ch][j]->getTextureEntry());//all textures are in use.
                }
            }

            if (te_list.empty())
            {
                mTextureList.clear();
                return;
            }

            auto end = te_list.size();

            for(std::list< LLPointer<LLViewerTexture> >::iterator iter = mTextureList.begin();
                iter != mTextureList.end(); ++iter)
            {
                size_t i = 0;

                for(i = 0; i < end; i++)
                {
                    if(te_list[i] && te_list[i]->getID() == (*iter)->getID())//the texture is in use.
                    {
                        te_list[i] = NULL;
                        break;
                    }
                }
                if(i == end) //no hit for this texture, remove it.
                {
// [SL:KB] - Patch: Render-TextureToggle (Catznip-5.2)
                    // See above
                    if (facep->isDefaultTexture(channel))
                    {
                        return;
                    }
// [/SL:KB]
                    mTextureList.erase(iter); //decrease the reference number for tex by one.
                    return;
                }
            }
        }
    }

    //check if it is a parcel media
    for(std::list< LLPointer<LLViewerTexture> >::iterator iter = mTextureList.begin();
                iter != mTextureList.end(); ++iter)
    {
        if((*iter)->getID() == mID)
        {
            mTextureList.erase(iter); //decrease the reference number for tex by one.
            return;
        }
    }

    if(te && te->getID().notNull()) //should have a texture but none found
    {
        LL_ERRS() << "mTextureList texture reference number is corrupted. Texture id: " << te->getID() << " List size: " << (U32)mTextureList.size() << LL_ENDL;
    }
}

void LLViewerMediaTexture::stopPlaying()
{
    // Don't stop the media impl playing here -- this breaks non-inworld media (login screen, search, and media browser).
//  if(mMediaImplp)
//  {
//      mMediaImplp->stop();
//  }
    mIsPlaying = false;
}

void LLViewerMediaTexture::switchTexture(U32 ch, LLFace* facep)
{
    if(facep)
    {
        //check if another media is playing on this face.
        if(facep->getTexture() && facep->getTexture() != this
            && facep->getTexture()->getType() == LLViewerTexture::MEDIA_TEXTURE)
        {
            if(mID == facep->getTexture()->getID()) //this is a parcel media
            {
                return; //let the prim media win.
            }
        }

        if(mIsPlaying) //old textures switch to the media texture
        {
            facep->switchTexture(ch, this);
        }
        else //switch to old textures.
        {
            const LLTextureEntry* te = facep->getTextureEntry();
            if(te)
            {
                LLViewerTexture* tex = te->getID().notNull() ? gTextureList.findImage(te->getID(), TEX_LIST_STANDARD) : NULL;
                if(!tex && te->getID() != mID)//try parcel media.
                {
                    tex = gTextureList.findImage(mID, TEX_LIST_STANDARD);
                }
                if(!tex)
                {
                    tex = LLViewerFetchedTexture::sDefaultImagep;
                }
                facep->switchTexture(ch, tex);
            }
        }
    }
}

void LLViewerMediaTexture::setPlaying(bool playing)
{
    if(!mMediaImplp)
    {
        return;
    }
    if(!playing && !mIsPlaying)
    {
        return; //media is already off
    }

    if(playing == mIsPlaying && !mMediaImplp->isUpdated())
    {
        return; //nothing has changed since last time.
    }

    mIsPlaying = playing;
    if(mIsPlaying) //is about to play this media
    {
        if(findFaces())
        {
            //about to update all faces.
            mMediaImplp->setUpdated(false);
        }

        if(mMediaFaceList.empty())//no face pointing to this media
        {
            stopPlaying();
            return;
        }

        for(std::list< LLFace* >::iterator iter = mMediaFaceList.begin(); iter!= mMediaFaceList.end(); ++iter)
        {
            LLFace* facep = *iter;
            switchTexture(LLRender::DIFFUSE_MAP, facep);
        }
    }
    else //stop playing this media
    {
        U32 ch = LLRender::DIFFUSE_MAP;

        llassert(mNumFaces[ch] <= mFaceList[ch].size());
        for(U32 i = mNumFaces[ch]; i; i--)
        {
            switchTexture(ch, mFaceList[ch][i - 1]); //current face could be removed in this function.
        }
    }
    return;
}

//virtual
F32 LLViewerMediaTexture::getMaxVirtualSize()
{
    if(LLFrameTimer::getFrameCount() == mUpdateVirtualSizeTime)
    {
        return mMaxVirtualSize;
    }
    mUpdateVirtualSizeTime = LLFrameTimer::getFrameCount();
    /* TommyTheTerrible - Not doing this anymore.
    if(!mMaxVirtualSizeResetCounter)
    {
        addTextureStats(0.f, false);//reset
    }
    */
    // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings, not happening with SL Viewer
    static LLCachedControl<F32> texture_camera_boost(gSavedSettings, "TextureCameraBoost", 7.f);
    F32 vsize = 0.0f;
    // </FS:minerjr> [FIRE-35081]
    if(mIsPlaying) //media is playing
    {
        for (U32 ch = 0; ch < LLRender::NUM_TEXTURE_CHANNELS; ++ch)
        {
            llassert(mNumFaces[ch] <= mFaceList[ch].size());
            for(U32 i = 0; i < mNumFaces[ch]; i++)
            {
                LLFace* facep = mFaceList[ch][i];
            if(facep->getDrawable()->isRecentlyVisible())
            {                
                // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
                //addTextureStats(facep->getVirtualSize());
                // Add the importance to camera and close to camera to the media texture
                vsize = facep->getVirtualSize();
                vsize = vsize + (vsize * facep->getImportanceToCamera() * texture_camera_boost);
                // Apply second boost based upon if the texture is close to the camera (< 16.1 meters * draw distance multiplier)
                vsize = vsize + (vsize * facep->getCloseToCamera() * texture_camera_boost);
                addTextureStats(vsize);
                // </FS:minerjr> [FIRE-35081]
            }
        }
    }
    }
    else //media is not in playing
    {
        findFaces();

        if(!mMediaFaceList.empty())
        {
            for(std::list< LLFace* >::iterator iter = mMediaFaceList.begin(); iter!= mMediaFaceList.end(); ++iter)
            {
                LLFace* facep = *iter;
                if(facep->getDrawable()->isRecentlyVisible())
                {
                    // <FS:minerjr> [FIRE-35081] Blurry prims not changing with graphics settings
                    //addTextureStats(facep->getVirtualSize());
                    // Add the importance to camera and close to camera to the media texture 
                    vsize = facep->getVirtualSize();
                    vsize = vsize + (vsize * facep->getImportanceToCamera() * texture_camera_boost);
                    // Apply second boost based upon if the texture is close to the camera (< 16.1 meters * draw distance multiplier)
                    vsize = vsize + (vsize * facep->getCloseToCamera() * texture_camera_boost);
                    addTextureStats(vsize);
                    // </FS:minerjr> [FIRE-35081]
                }
            }
        }
    }

    if(mMaxVirtualSizeResetCounter > 0)
    {
        mMaxVirtualSizeResetCounter--;
    }
    reorganizeFaceList();
    reorganizeVolumeList();

    return mMaxVirtualSize;
}
//----------------------------------------------------------------------------------------------
//end of LLViewerMediaTexture
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
//start of LLTexturePipelineTester
//----------------------------------------------------------------------------------------------
LLTexturePipelineTester::LLTexturePipelineTester() : LLMetricPerformanceTesterWithSession(sTesterName)
{
    addMetric("TotalBytesLoaded");
    addMetric("TotalBytesLoadedFromCache");
    addMetric("TotalBytesLoadedForLargeImage");
    addMetric("TotalBytesLoadedForSculpties");
    addMetric("StartFetchingTime");
    addMetric("TotalGrayTime");
    addMetric("TotalStablizingTime");
    addMetric("StartTimeLoadingSculpties");
    addMetric("EndTimeLoadingSculpties");

    addMetric("Time");
    addMetric("TotalBytesBound");
    addMetric("TotalBytesBoundForLargeImage");
    addMetric("PercentageBytesBound");

    mTotalBytesLoaded = (S32Bytes)0;
    mTotalBytesLoadedFromCache = (S32Bytes)0;
    mTotalBytesLoadedForLargeImage = (S32Bytes)0;
    mTotalBytesLoadedForSculpties = (S32Bytes)0;

    reset();
}

LLTexturePipelineTester::~LLTexturePipelineTester()
{
    LLViewerTextureManager::sTesterp = NULL;
}

void LLTexturePipelineTester::update()
{
    mLastTotalBytesUsed = mTotalBytesUsed;
    mLastTotalBytesUsedForLargeImage = mTotalBytesUsedForLargeImage;
    mTotalBytesUsed = (S32Bytes)0;
    mTotalBytesUsedForLargeImage = (S32Bytes)0;

    if(LLAppViewer::getTextureFetch()->getNumRequests() > 0) //fetching list is not empty
    {
        if(mPause)
        {
            //start a new fetching session
            reset();
            mStartFetchingTime = LLImageGL::sLastFrameTime;
            mPause = false;
        }

        //update total gray time
        if(mUsingDefaultTexture)
        {
            mUsingDefaultTexture = false;
            mTotalGrayTime = LLImageGL::sLastFrameTime - mStartFetchingTime;
        }

        //update the stablizing timer.
        updateStablizingTime();

        outputTestResults();
    }
    else if(!mPause)
    {
        //stop the current fetching session
        mPause = true;
        outputTestResults();
        reset();
    }
}

void LLTexturePipelineTester::reset()
{
    mPause = true;

    mUsingDefaultTexture = false;
    mStartStablizingTime = 0.0f;
    mEndStablizingTime = 0.0f;

    mTotalBytesUsed = (S32Bytes)0;
    mTotalBytesUsedForLargeImage = (S32Bytes)0;
    mLastTotalBytesUsed = (S32Bytes)0;
    mLastTotalBytesUsedForLargeImage = (S32Bytes)0;

    mStartFetchingTime = 0.0f;

    mTotalGrayTime = 0.0f;
    mTotalStablizingTime = 0.0f;

    mStartTimeLoadingSculpties = 1.0f;
    mEndTimeLoadingSculpties = 0.0f;
}

//virtual
void LLTexturePipelineTester::outputTestRecord(LLSD *sd)
{
    std::string currentLabel = getCurrentLabelName();
    (*sd)[currentLabel]["TotalBytesLoaded"]              = (LLSD::Integer)mTotalBytesLoaded.value();
    (*sd)[currentLabel]["TotalBytesLoadedFromCache"]     = (LLSD::Integer)mTotalBytesLoadedFromCache.value();
    (*sd)[currentLabel]["TotalBytesLoadedForLargeImage"] = (LLSD::Integer)mTotalBytesLoadedForLargeImage.value();
    (*sd)[currentLabel]["TotalBytesLoadedForSculpties"]  = (LLSD::Integer)mTotalBytesLoadedForSculpties.value();

    (*sd)[currentLabel]["StartFetchingTime"]             = (LLSD::Real)mStartFetchingTime;
    (*sd)[currentLabel]["TotalGrayTime"]                 = (LLSD::Real)mTotalGrayTime;
    (*sd)[currentLabel]["TotalStablizingTime"]           = (LLSD::Real)mTotalStablizingTime;

    (*sd)[currentLabel]["StartTimeLoadingSculpties"]     = (LLSD::Real)mStartTimeLoadingSculpties;
    (*sd)[currentLabel]["EndTimeLoadingSculpties"]       = (LLSD::Real)mEndTimeLoadingSculpties;

    (*sd)[currentLabel]["Time"]                          = LLImageGL::sLastFrameTime;
    (*sd)[currentLabel]["TotalBytesBound"]               = (LLSD::Integer)mLastTotalBytesUsed.value();
    (*sd)[currentLabel]["TotalBytesBoundForLargeImage"]  = (LLSD::Integer)mLastTotalBytesUsedForLargeImage.value();
    (*sd)[currentLabel]["PercentageBytesBound"]          = (LLSD::Real)(100.f * mLastTotalBytesUsed / mTotalBytesLoaded);
}

void LLTexturePipelineTester::updateTextureBindingStats(const LLViewerTexture* imagep)
{
    U32Bytes mem_size = imagep->getTextureMemory();
    mTotalBytesUsed += mem_size;

    if(MIN_LARGE_IMAGE_AREA <= (U32)(mem_size.value() / (U32)imagep->getComponents()))
    {
        mTotalBytesUsedForLargeImage += mem_size;
    }
}

void LLTexturePipelineTester::updateTextureLoadingStats(const LLViewerFetchedTexture* imagep, const LLImageRaw* raw_imagep, bool from_cache)
{
    U32Bytes data_size = (U32Bytes)raw_imagep->getDataSize();
    mTotalBytesLoaded += data_size;

    if(from_cache)
    {
        mTotalBytesLoadedFromCache += data_size;
    }

    if(MIN_LARGE_IMAGE_AREA <= (U32)(data_size.value() / (U32)raw_imagep->getComponents()))
    {
        mTotalBytesLoadedForLargeImage += data_size;
    }

    if(imagep->forSculpt())
    {
        mTotalBytesLoadedForSculpties += data_size;

        if(mStartTimeLoadingSculpties > mEndTimeLoadingSculpties)
        {
            mStartTimeLoadingSculpties = LLImageGL::sLastFrameTime;
        }
        mEndTimeLoadingSculpties = LLImageGL::sLastFrameTime;
    }
}

void LLTexturePipelineTester::updateGrayTextureBinding()
{
    mUsingDefaultTexture = true;
}

void LLTexturePipelineTester::setStablizingTime()
{
    if(mStartStablizingTime <= mStartFetchingTime)
    {
        mStartStablizingTime = LLImageGL::sLastFrameTime;
    }
    mEndStablizingTime = LLImageGL::sLastFrameTime;
}

void LLTexturePipelineTester::updateStablizingTime()
{
    if(mStartStablizingTime > mStartFetchingTime)
    {
        F32 t = mEndStablizingTime - mStartStablizingTime;

        if(t > F_ALMOST_ZERO && (t - mTotalStablizingTime) < F_ALMOST_ZERO)
        {
            //already stablized
            mTotalStablizingTime = LLImageGL::sLastFrameTime - mStartStablizingTime;

            //cancel the timer
            mStartStablizingTime = 0.f;
            mEndStablizingTime = 0.f;
        }
        else
        {
            mTotalStablizingTime = t;
        }
    }
    mTotalStablizingTime = 0.f;
}

//virtual
void LLTexturePipelineTester::compareTestSessions(llofstream* os)
{
    LLTexturePipelineTester::LLTextureTestSession* base_sessionp = dynamic_cast<LLTexturePipelineTester::LLTextureTestSession*>(mBaseSessionp);
    LLTexturePipelineTester::LLTextureTestSession* current_sessionp = dynamic_cast<LLTexturePipelineTester::LLTextureTestSession*>(mCurrentSessionp);
    if(!base_sessionp || !current_sessionp)
    {
        LL_ERRS() << "type of test session does not match!" << LL_ENDL;
    }

    //compare and output the comparison
    *os << llformat("%s\n", getTesterName().c_str());
    *os << llformat("AggregateResults\n");

    compareTestResults(os, "TotalGrayTime", base_sessionp->mTotalGrayTime, current_sessionp->mTotalGrayTime);
    compareTestResults(os, "TotalStablizingTime", base_sessionp->mTotalStablizingTime, current_sessionp->mTotalStablizingTime);
    compareTestResults(os, "StartTimeLoadingSculpties", base_sessionp->mStartTimeLoadingSculpties, current_sessionp->mStartTimeLoadingSculpties);
    compareTestResults(os, "TotalTimeLoadingSculpties", base_sessionp->mTotalTimeLoadingSculpties, current_sessionp->mTotalTimeLoadingSculpties);

    compareTestResults(os, "TotalBytesLoaded", base_sessionp->mTotalBytesLoaded, current_sessionp->mTotalBytesLoaded);
    compareTestResults(os, "TotalBytesLoadedFromCache", base_sessionp->mTotalBytesLoadedFromCache, current_sessionp->mTotalBytesLoadedFromCache);
    compareTestResults(os, "TotalBytesLoadedForLargeImage", base_sessionp->mTotalBytesLoadedForLargeImage, current_sessionp->mTotalBytesLoadedForLargeImage);
    compareTestResults(os, "TotalBytesLoadedForSculpties", base_sessionp->mTotalBytesLoadedForSculpties, current_sessionp->mTotalBytesLoadedForSculpties);

    *os << llformat("InstantResults\n");
    S32 size = llmin(base_sessionp->mInstantPerformanceListCounter, current_sessionp->mInstantPerformanceListCounter);
    for(S32 i = 0; i < size; i++)
    {
        *os << llformat("Time(B-T)-%.4f-%.4f\n", base_sessionp->mInstantPerformanceList[i].mTime, current_sessionp->mInstantPerformanceList[i].mTime);

        compareTestResults(os, "AverageBytesUsedPerSecond", base_sessionp->mInstantPerformanceList[i].mAverageBytesUsedPerSecond,
            current_sessionp->mInstantPerformanceList[i].mAverageBytesUsedPerSecond);

        compareTestResults(os, "AverageBytesUsedForLargeImagePerSecond", base_sessionp->mInstantPerformanceList[i].mAverageBytesUsedForLargeImagePerSecond,
            current_sessionp->mInstantPerformanceList[i].mAverageBytesUsedForLargeImagePerSecond);

        compareTestResults(os, "AveragePercentageBytesUsedPerSecond", base_sessionp->mInstantPerformanceList[i].mAveragePercentageBytesUsedPerSecond,
            current_sessionp->mInstantPerformanceList[i].mAveragePercentageBytesUsedPerSecond);
    }

    if(size < base_sessionp->mInstantPerformanceListCounter)
    {
        for(S32 i = size; i < base_sessionp->mInstantPerformanceListCounter; i++)
        {
            *os << llformat("Time(B-T)-%.4f- \n", base_sessionp->mInstantPerformanceList[i].mTime);

            *os << llformat(", AverageBytesUsedPerSecond, %d, N/A \n", base_sessionp->mInstantPerformanceList[i].mAverageBytesUsedPerSecond);
            *os << llformat(", AverageBytesUsedForLargeImagePerSecond, %d, N/A \n", base_sessionp->mInstantPerformanceList[i].mAverageBytesUsedForLargeImagePerSecond);
            *os << llformat(", AveragePercentageBytesUsedPerSecond, %.4f, N/A \n", base_sessionp->mInstantPerformanceList[i].mAveragePercentageBytesUsedPerSecond);
        }
    }
    else if(size < current_sessionp->mInstantPerformanceListCounter)
    {
        for(S32 i = size; i < current_sessionp->mInstantPerformanceListCounter; i++)
        {
            *os << llformat("Time(B-T)- -%.4f\n", current_sessionp->mInstantPerformanceList[i].mTime);

            *os << llformat(", AverageBytesUsedPerSecond, N/A, %d\n", current_sessionp->mInstantPerformanceList[i].mAverageBytesUsedPerSecond);
            *os << llformat(", AverageBytesUsedForLargeImagePerSecond, N/A, %d\n", current_sessionp->mInstantPerformanceList[i].mAverageBytesUsedForLargeImagePerSecond);
            *os << llformat(", AveragePercentageBytesUsedPerSecond, N/A, %.4f\n", current_sessionp->mInstantPerformanceList[i].mAveragePercentageBytesUsedPerSecond);
        }
    }
}

//virtual
LLMetricPerformanceTesterWithSession::LLTestSession* LLTexturePipelineTester::loadTestSession(LLSD* log)
{
    LLTexturePipelineTester::LLTextureTestSession* sessionp = new LLTexturePipelineTester::LLTextureTestSession();
    if(!sessionp)
    {
        return NULL;
    }

    F32 total_gray_time = 0.f;
    F32 total_stablizing_time = 0.f;
    F32 total_loading_sculpties_time = 0.f;

    F32 start_fetching_time = -1.f;
    F32 start_fetching_sculpties_time = 0.f;

    F32 last_time = 0.0f;
    S32 frame_count = 0;

    sessionp->mInstantPerformanceListCounter = 0;
    sessionp->mInstantPerformanceList.resize(128);
    sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedPerSecond = 0;
    sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedForLargeImagePerSecond = 0;
    sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAveragePercentageBytesUsedPerSecond = 0.f;
    sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mTime = 0.f;

    //load a session
    std::string currentLabel = getCurrentLabelName();
    bool in_log = (*log).has(currentLabel);
    while (in_log)
    {
        LLSD::String label = currentLabel;

        if(sessionp->mInstantPerformanceListCounter >= (S32)sessionp->mInstantPerformanceList.size())
        {
            sessionp->mInstantPerformanceList.resize(sessionp->mInstantPerformanceListCounter + 128);
        }

        //time
        F32 start_time = (F32)(*log)[label]["StartFetchingTime"].asReal();
        F32 cur_time   = (F32)(*log)[label]["Time"].asReal();
        if(start_time - start_fetching_time > F_ALMOST_ZERO) //fetching has paused for a while
        {
            sessionp->mTotalGrayTime += total_gray_time;
            sessionp->mTotalStablizingTime += total_stablizing_time;

            sessionp->mStartTimeLoadingSculpties = start_fetching_sculpties_time;
            sessionp->mTotalTimeLoadingSculpties += total_loading_sculpties_time;

            start_fetching_time = start_time;
            total_gray_time = 0.f;
            total_stablizing_time = 0.f;
            total_loading_sculpties_time = 0.f;
        }
        else
        {
            total_gray_time = (F32)(*log)[label]["TotalGrayTime"].asReal();
            total_stablizing_time = (F32)(*log)[label]["TotalStablizingTime"].asReal();

            total_loading_sculpties_time = (F32)(*log)[label]["EndTimeLoadingSculpties"].asReal() - (F32)(*log)[label]["StartTimeLoadingSculpties"].asReal();
            if(start_fetching_sculpties_time < 0.f && total_loading_sculpties_time > 0.f)
            {
                start_fetching_sculpties_time = (F32)(*log)[label]["StartTimeLoadingSculpties"].asReal();
            }
        }

        //total loaded bytes
        sessionp->mTotalBytesLoaded = (*log)[label]["TotalBytesLoaded"].asInteger();
        sessionp->mTotalBytesLoadedFromCache = (*log)[label]["TotalBytesLoadedFromCache"].asInteger();
        sessionp->mTotalBytesLoadedForLargeImage = (*log)[label]["TotalBytesLoadedForLargeImage"].asInteger();
        sessionp->mTotalBytesLoadedForSculpties = (*log)[label]["TotalBytesLoadedForSculpties"].asInteger();

        //instant metrics
        sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedPerSecond +=
            (*log)[label]["TotalBytesBound"].asInteger();
        sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedForLargeImagePerSecond +=
            (*log)[label]["TotalBytesBoundForLargeImage"].asInteger();
        sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAveragePercentageBytesUsedPerSecond +=
            (F32)(*log)[label]["PercentageBytesBound"].asReal();
        frame_count++;
        if(cur_time - last_time >= 1.0f)
        {
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedPerSecond /= frame_count;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedForLargeImagePerSecond /= frame_count;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAveragePercentageBytesUsedPerSecond /= frame_count;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mTime = last_time;

            frame_count = 0;
            last_time = cur_time;
            sessionp->mInstantPerformanceListCounter++;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedPerSecond = 0;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAverageBytesUsedForLargeImagePerSecond = 0;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mAveragePercentageBytesUsedPerSecond = 0.f;
            sessionp->mInstantPerformanceList[sessionp->mInstantPerformanceListCounter].mTime = 0.f;
        }
        // Next label
        incrementCurrentCount();
        currentLabel = getCurrentLabelName();
        in_log = (*log).has(currentLabel);
    }

    sessionp->mTotalGrayTime += total_gray_time;
    sessionp->mTotalStablizingTime += total_stablizing_time;

    if(sessionp->mStartTimeLoadingSculpties < 0.f)
    {
        sessionp->mStartTimeLoadingSculpties = start_fetching_sculpties_time;
    }
    sessionp->mTotalTimeLoadingSculpties += total_loading_sculpties_time;

    return sessionp;
}

LLTexturePipelineTester::LLTextureTestSession::LLTextureTestSession()
{
    reset();
}
LLTexturePipelineTester::LLTextureTestSession::~LLTextureTestSession()
{
}
void LLTexturePipelineTester::LLTextureTestSession::reset()
{
    mTotalGrayTime = 0.0f;
    mTotalStablizingTime = 0.0f;

    mStartTimeLoadingSculpties = 0.0f;
    mTotalTimeLoadingSculpties = 0.0f;

    mTotalBytesLoaded = 0;
    mTotalBytesLoadedFromCache = 0;
    mTotalBytesLoadedForLargeImage = 0;
    mTotalBytesLoadedForSculpties = 0;

    mInstantPerformanceListCounter = 0;
}
//----------------------------------------------------------------------------------------------
//end of LLTexturePipelineTester
//----------------------------------------------------------------------------------------------

