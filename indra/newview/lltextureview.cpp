/**
 * @file lltextureview.cpp
 * @brief LLTextureView class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

#include <set>

#include "lltextureview.h"

#include "llrect.h"
#include "llerror.h"
#include "lllfsthread.h"
#include "llui.h"
#include "llimageworker.h"
#include "llrender.h"

#include "lltooltip.h"
#include "llappviewer.h"
#include "llmeshrepository.h"
#include "llselectmgr.h"
#include "llviewertexlayer.h"
#include "lltexturecache.h"
#include "lltexturefetch.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewertexture.h"
#include "llviewertexturelist.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llvovolume.h"
#include "llviewerstats.h"
#include "llworld.h"

// For avatar texture view
#include "llvoavatarself.h"
#include "lltexlayer.h"

LLTextureView *gTextureView = NULL;

#define HIGH_PRIORITY 100000000.f

//static
std::set<LLViewerFetchedTexture*> LLTextureView::sDebugImages;

////////////////////////////////////////////////////////////////////////////

static std::string title_string1a("UUID       Area D(R)   Imp FFT(Bst) s/h/p   Download pk/max");
static std::string title_string1b("Tex UUID Area  DDis(Req)  Fetch(DecodePri)     [download] pk/max");
static std::string title_string2("State");
static std::string title_string3("Pkt Bnd");
static std::string title_string4("  W x H (Dis) Mem");

static S32 title_x1 = 0;
static S32 title_x2 = 460;
static S32 title_x3 = title_x2 + 40;
static S32 title_x4 = title_x3 + 46;
static S32 texture_bar_height = 8;

////////////////////////////////////////////////////////////////////////////

class LLTextureBar : public LLView
{
public:
    LLPointer<LLViewerFetchedTexture> mImagep;
    S32 mHilite;

public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*> texture_view;
        Params()
        :   texture_view("texture_view")
        {
            changeDefault(mouse_opaque, false);
        }
    };
    LLTextureBar(const Params& p)
    :   LLView(p),
        mHilite(0),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

// Used for sorting
    struct sort
    {
        bool operator()(const LLView* i1, const LLView* i2)
        {
            LLTextureBar* bar1p = (LLTextureBar*)i1;
            LLTextureBar* bar2p = (LLTextureBar*)i2;
            LLViewerFetchedTexture *i1p = bar1p->mImagep;
            LLViewerFetchedTexture *i2p = bar2p->mImagep;
            F32 pri1 = i1p->getMaxVirtualSize();
            F32 pri2 = i2p->getMaxVirtualSize();
            if (pri1 > pri2)
                return true;
            else if (pri2 > pri1)
                return false;
            else
                return i1p->getID() < i2p->getID();
        }
    };

    struct sort_fetch
    {
        bool operator()(const LLView* i1, const LLView* i2)
        {
            LLTextureBar* bar1p = (LLTextureBar*)i1;
            LLTextureBar* bar2p = (LLTextureBar*)i2;
            LLViewerFetchedTexture *i1p = bar1p->mImagep;
            LLViewerFetchedTexture *i2p = bar2p->mImagep;
            U32 pri1 = i1p->getFetchPriority() ;
            U32 pri2 = i2p->getFetchPriority() ;
            if (pri1 > pri2)
                return true;
            else if (pri2 > pri1)
                return false;
            else
                return i1p->getID() < i2p->getID();
        }
    };
private:
    LLTextureView* mTextureView;
};

void LLTextureBar::draw()
{
    if (!mImagep)
    {
        return;
    }

    LLColor4 color;
    if (mImagep->getID() == LLAppViewer::getTextureFetch()->mDebugID)
    {
        color = LLColor4::cyan2;
    }
    else if (mHilite)
    {
        S32 idx = llclamp(mHilite,1,3);
        if (idx==1) color = LLColor4::orange;
        else if (idx==2) color = LLColor4::yellow;
        else color = LLColor4::pink2;
    }
    else if (mImagep->mDontDiscard)
    {
        color = LLColor4::green4;
    }
    else if (mImagep->getMaxVirtualSize() <= 0.0f)
    {
        color = LLColor4::grey; color[VALPHA] = .7f;
    }
    else
    {
        color = LLColor4::white; color[VALPHA] = .7f;
    }

    // We need to draw:
    // The texture UUID or name
    // The progress bar for the texture, highlighted if it's being download
    // Various numerical stats.
    std::string tex_str;
    S32 left, right;
    S32 top = 0;
    S32 bottom = top + 6;
    LLColor4 clr;

    LLGLSUIDefault gls_ui;

    // Name, pixel_area, requested pixel area, decode priority
    std::string uuid_str;
    mImagep->mID.toString(uuid_str);
    uuid_str = uuid_str.substr(0,7);
    std::string boost_space;
    if (mImagep->mBoostLevel < 10)
        boost_space = " "; // Formating space to keep columns in line when boost is one digit

    tex_str = llformat("%s %7.0f %d(%d)  %0.2f  %d(%d) %s  %d/%d/%d",
        uuid_str.c_str(),
        mImagep->mMaxVirtualSize,
        mImagep->mDesiredDiscardLevel,
        mImagep->mRequestedDiscardLevel,
        mImagep->mMaxFaceImportance,
        mImagep->mFTType,
        mImagep->mBoostLevel,
        boost_space.c_str(),
        mImagep->mForSculpt,
        mImagep->mForHUD,
        mImagep->mForParticle
    );


    LLFontGL::getFontMonospace()->renderUTF8(tex_str, 0, title_x1, getRect().getHeight(),
                                     color, LLFontGL::LEFT, LLFontGL::TOP);

    // State
    // Hack: mirrored from lltexturefetch.cpp
    struct { const std::string desc; LLColor4 color; } fetch_state_desc[] = {
        { "---", LLColor4::red },   // INVALID
        { "INI", LLColor4::white }, // INIT
        // <FS:Ansariel> Unique state codes
        //{ "DSK", LLColor4::cyan },    // LOAD_FROM_TEXTURE_CACHE
        { "CCH", LLColor4::cyan },  // LOAD_FROM_TEXTURE_CACHE
        { "DSK", LLColor4::blue },  // CACHE_POST
        { "NET", LLColor4::green }, // LOAD_FROM_NETWORK
        { "SIM", LLColor4::green }, // LOAD_FROM_SIMULATOR // <FS:Ansariel> OpenSim compatibility
        { "HTW", LLColor4::green }, // WAIT_HTTP_RESOURCE
        // <FS:Ansariel> Unique state codes
        //{ "HTW", LLColor4::green },   // WAIT_HTTP_RESOURCE2
        { "HTI", LLColor4::green }, // WAIT_HTTP_RESOURCE2
        { "REQ", LLColor4::yellow },// SEND_HTTP_REQ
        { "HTP", LLColor4::green }, // WAIT_HTTP_REQ
        { "DEC", LLColor4::yellow },// DECODE_IMAGE
        { "DEU", LLColor4::green }, // DECODE_IMAGE_UPDATE
        { "WRT", LLColor4::purple },// WRITE_TO_CACHE
        { "WWT", LLColor4::orange },// WAIT_ON_WRITE
        { "END", LLColor4::red },   // DONE
#define LAST_STATE 14
        { "CRE", LLColor4::magenta }, // LAST_STATE+1
        { "FUL", LLColor4::green }, // LAST_STATE+2
        { "BAD", LLColor4::red }, // LAST_STATE+3
        { "MIS", LLColor4::red }, // LAST_STATE+4
        { "---", LLColor4::white }, // LAST_STATE+5
    };
    const S32 fetch_state_desc_size = (S32)LL_ARRAY_SIZE(fetch_state_desc);
    S32 state =
        mImagep->mNeedsCreateTexture ? LAST_STATE+1 :
        mImagep->mFullyLoaded ? LAST_STATE+2 :
        //mImagep->mMinDiscardLevel > 0 ? LAST_STATE+3 : // <TS:3T> Stop expecting all new discards to be lower
        mImagep->mIsMissingAsset ? LAST_STATE+4 :
        !mImagep->mIsFetching ? LAST_STATE+5 :
        mImagep->mFetchState;
    state = llclamp(state,0,fetch_state_desc_size-1);

    LLFontGL::getFontMonospace()->renderUTF8(fetch_state_desc[state].desc, 0, title_x2, getRect().getHeight(),
                                     fetch_state_desc[state].color,
                                     LLFontGL::LEFT, LLFontGL::TOP);
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // Draw the progress bar.
    S32 bar_width = 100;
    S32 bar_left = 280;
    left = bar_left;
    right = left + bar_width;

    gGL.color4f(0.f, 0.f, 0.f, 0.75f);
    gl_rect_2d(left, top, right, bottom);

    F32 data_progress = mImagep->mDownloadProgress;

    if (data_progress > 0.0f)
    {
        // Downloaded bytes
        right = left + llfloor(data_progress * (F32)bar_width);
        if (right > left)
        {
            gGL.color4f(0.f, 0.f, 1.f, 0.75f);
            gl_rect_2d(left, top, right, bottom);
        }
    }

    S32 pip_width = 6;
    S32 pip_space = 14;
    S32 pip_x = title_x3 + pip_space/2;

    // Draw the packet pip
    const F32 pip_max_time = 5.f;
    F32 last_event = mImagep->mLastPacketTimer.getElapsedTimeF32();
    if (last_event < pip_max_time)
    {
        clr = LLColor4::white;
    }
    else
    {
        last_event = mImagep->mRequestDeltaTime;
        if (last_event < pip_max_time)
        {
            clr = LLColor4::green;
        }
        else
        {
            last_event = mImagep->mFetchDeltaTime;
            if (last_event < pip_max_time)
            {
                clr = LLColor4::yellow;
            }
        }
    }
    if (last_event < pip_max_time)
    {
        clr.setAlpha(1.f - last_event/pip_max_time);
        gGL.color4fv(clr.mV);
        gl_rect_2d(pip_x, top, pip_x + pip_width, bottom);
    }
    pip_x += pip_width + pip_space;

    // we don't want to show bind/resident pips for textures using the default texture
    if (mImagep->hasGLTexture())
    {
        // Draw the bound pip
        last_event = mImagep->getTimePassedSinceLastBound();
        if (last_event < 1.f)
        {
            clr = mImagep->getMissed() ? LLColor4::red : LLColor4::magenta1;
            clr.setAlpha(1.f - last_event);
            gGL.color4fv(clr.mV);
            gl_rect_2d(pip_x, top, pip_x + pip_width, bottom);
        }
    }
    pip_x += pip_width + pip_space;


    {
        LLGLSUIDefault gls_ui;
        // draw the image size at the end
        {
            std::string num_str = llformat("%3dx%3d (%2d) %7d", mImagep->getWidth(), mImagep->getHeight(),
                mImagep->getDiscardLevel(), mImagep->hasGLTexture() ? mImagep->getTextureMemory().value() : 0);
            LLFontGL::getFontMonospace()->renderUTF8(num_str, 0, title_x4, getRect().getHeight(), color,
                                            LLFontGL::LEFT, LLFontGL::TOP);
        }
    }

}

bool LLTextureBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == MASK_ALT)
    {
        LLAppViewer::getTextureFetch()->mDebugID = mImagep->getID();
        return true;
    }
    return LLView::handleMouseDown(x,y,mask);
}

LLRect LLTextureBar::getRequiredRect()
{
    LLRect rect;

    rect.mTop = texture_bar_height;

    return rect;
}

////////////////////////////////////////////////////////////////////////////

class LLAvatarTexBar : public LLView
{
public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*>   texture_view;
        Params()
        :   texture_view("texture_view")
        {
            S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
            changeDefault(rect, LLRect(0,0,100,line_height * 4));
        }
    };

    LLAvatarTexBar(const Params& p)
    :   LLView(p),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

private:
    LLTextureView* mTextureView;
};

void LLAvatarTexBar::draw()
{
    // <FS:Ansariel> Speed-up
    //if (!gSavedSettings.getBOOL("DebugAvatarRezTime")) return;
    static LLCachedControl<bool> debugAvatarRezTime(gSavedSettings, "DebugAvatarRezTime");
    if (!debugAvatarRezTime) return;

    LLVOAvatarSelf* avatarp = gAgentAvatarp;
    if (!avatarp) return;

    const S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
    const S32 v_offset = 0;
    const S32 l_offset = 3;

    //----------------------------------------------------------------------------
    LLGLSUIDefault gls_ui;
    LLColor4 color;

    U32 line_num = 1;
    for (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::BakedTextures::const_iterator baked_iter = LLAvatarAppearance::getDictionary()->getBakedTextures().begin();
         baked_iter != LLAvatarAppearance::getDictionary()->getBakedTextures().end();
         ++baked_iter)
    {
        const LLAvatarAppearanceDefines::EBakedTextureIndex baked_index = baked_iter->first;
        const LLViewerTexLayerSet *layerset = avatarp->debugGetLayerSet(baked_index);
        if (!layerset) continue;
        const LLViewerTexLayerSetBuffer *layerset_buffer = layerset->getViewerComposite();
        if (!layerset_buffer) continue;

        LLColor4 text_color = LLColor4::white;

        // <FS:Ansariel> [Legacy Bake]
        if (layerset_buffer->uploadNeeded())
        {
            text_color = LLColor4::red;
        }
        if (layerset_buffer->uploadInProgress())
        {
            text_color = LLColor4::magenta;
        }
        // </FS:Ansariel> [Legacy Bake]

        std::string text = layerset_buffer->dumpTextureInfo();
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, l_offset, v_offset + line_height*line_num,
                                                 text_color, LLFontGL::LEFT, LLFontGL::TOP); //, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
        line_num++;
    }
    // <FS:Ansariel> Replace frequently called gSavedSettings
    //const U32 texture_timeout = gSavedSettings.getU32("AvatarBakedTextureUploadTimeout");
    //const U32 override_tex_discard_level = gSavedSettings.getU32("TextureDiscardLevel");
    static LLCachedControl<U32> sAvatarBakedTextureUploadTimeout(gSavedSettings, "AvatarBakedTextureUploadTimeout");
    static LLCachedControl<U32> sTextureDiscardLevel(gSavedSettings, "TextureDiscardLevel");
    const U32 texture_timeout = sAvatarBakedTextureUploadTimeout();
    const U32 override_tex_discard_level = sTextureDiscardLevel();
    // </FS:Ansariel>

    LLColor4 header_color(1.f, 1.f, 1.f, 0.9f);

    const std::string texture_timeout_str = texture_timeout ? llformat("%d", texture_timeout) : "Disabled";
    const std::string override_tex_discard_level_str = override_tex_discard_level ? llformat("%d",override_tex_discard_level) : "Disabled";
    std::string header_text = llformat("[ Timeout('AvatarBakedTextureUploadTimeout'):%s ] [ LOD_Override('TextureDiscardLevel'):%s ]", texture_timeout_str.c_str(), override_tex_discard_level_str.c_str());
    LLFontGL::getFontMonospace()->renderUTF8(header_text, 0, l_offset, v_offset + line_height*line_num,
                                             header_color, LLFontGL::LEFT, LLFontGL::TOP); //, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
    line_num++;
    std::string section_text = "Avatar Textures Information:";
    LLFontGL::getFontMonospace()->renderUTF8(section_text, 0, 0, v_offset + line_height*line_num,
                                             header_color, LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
}

bool LLAvatarTexBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    return false;
}

LLRect LLAvatarTexBar::getRequiredRect()
{
    LLRect rect;
    rect.mTop = 100;
    if (!gSavedSettings.getBOOL("DebugAvatarRezTime")) rect.mTop = 0;
    return rect;
}

////////////////////////////////////////////////////////////////////////////

class LLGLTexMemBar : public LLView
{
public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*>   texture_view;
        Params()
        :   texture_view("texture_view")
        {
            S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
            changeDefault(rect, LLRect(0,0,0,line_height * 7));
        }
    };

    LLGLTexMemBar(const Params& p)
    :   LLView(p),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

private:
    LLTextureView* mTextureView;
};

void LLGLTexMemBar::draw()
{
    F32 discard_bias = LLViewerTexture::sDesiredDiscardBias;
    F32 cache_usage = (F32)LLAppViewer::getTextureCache()->getUsage().valueInUnits<LLUnits::Megabytes>();
    F32 cache_max_usage = (F32)LLAppViewer::getTextureCache()->getMaxUsage().valueInUnits<LLUnits::Megabytes>();
    S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
    S32 v_offset = 0;//(S32)((texture_bar_height + 2.2f) * mTextureView->mNumTextureBars + 2.0f);
    F32Bytes total_texture_downloaded = gTotalTextureData;
    F32Bytes total_object_downloaded = gTotalObjectData;
    U32 total_http_requests = LLAppViewer::getTextureFetch()->getTotalNumHTTPRequests();
    U32 total_active_cached_objects = LLWorld::getInstance()->getNumOfActiveCachedObjects();
    U32 total_objects = gObjectList.getNumObjects();
    F32 x_right = 0.0;

    U32 image_count = gTextureList.getNumImages();
    U32 raw_image_count = 0;
    U64 raw_image_bytes = 0;

    U32 saved_raw_image_count = 0;
    U64 saved_raw_image_bytes = 0;

    U32 aux_raw_image_count = 0;
    U64 aux_raw_image_bytes = 0;

    for (auto& image : gTextureList)
    {
        const LLImageRaw* raw_image = image->getRawImage();

        if (raw_image)
        {
            raw_image_count++;
            raw_image_bytes += raw_image->getDataSize();
        }

        raw_image = image->getSavedRawImage();
        if (raw_image)
        {
            saved_raw_image_count++;
            saved_raw_image_bytes += raw_image->getDataSize();
        }

        raw_image = image->getAuxRawImage();
        if (raw_image)
        {
            aux_raw_image_count++;
            aux_raw_image_bytes += raw_image->getDataSize();
        }
    }

   F64 raw_image_bytes_MB = raw_image_bytes / (1024.0 * 1024.0);
   F64 saved_raw_image_bytes_MB = saved_raw_image_bytes / (1024.0 * 1024.0);
   F64 aux_raw_image_bytes_MB = aux_raw_image_bytes / (1024.0 * 1024.0);
   F64 texture_bytes_alloc = LLImageGL::getTextureBytesAllocated() / 1024.0 / 1024.0 * 1.3333f;
   F64 vertex_bytes_alloc = LLVertexBuffer::getBytesAllocated() / 1024.0 / 1024.0;
   F64 render_bytes_alloc = LLRenderTarget::sBytesAllocated / 1024.0 / 1024.0;

    //----------------------------------------------------------------------------
    LLGLSUIDefault gls_ui;
    LLColor4 text_color(1.f, 1.f, 1.f, 0.75f);
    LLColor4 color;

    std::string text = "";

    LLTrace::Recording& recording = LLViewerStats::instance().getRecording();

    F64 cacheHits     = recording.getSampleCount(LLTextureFetch::sCacheHit);
    F64 cacheAttempts = recording.getSampleCount(LLTextureFetch::sCacheAttempt);

    F32 cacheHitRate = (cacheAttempts > 0.0) ? F32((cacheHits / cacheAttempts) * 100.0f) : 0.0f;

    U32 cacheReadLatMin = U32(recording.getMin(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);
    U32 cacheReadLatMed = U32(recording.getMean(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);
    U32 cacheReadLatMax = U32(recording.getMax(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);

    U32 texDecodeLatMin = U32(recording.getMin(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);
    U32 texDecodeLatMed = U32(recording.getMean(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);
    U32 texDecodeLatMax = U32(recording.getMax(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);

    U32 texFetchLatMin = U32(recording.getMin(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);
    U32 texFetchLatMed = U32(recording.getMean(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);
    U32 texFetchLatMax = U32(recording.getMax(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);

    // draw a background above first line.... no idea where the rest of the background comes from for the below text
    gGL.color4f(0.f, 0.f, 0.f, 0.25f);
    gl_rect_2d(-10, getRect().getHeight() + line_height*2 + 1, getRect().getWidth()+2, getRect().getHeight()+2);

    text = llformat("Est. Free: %d MB Sys Free: %d MB GL Tex: %d MB FBO: %d MB Probe#: %d Probe Mem: %d MB Bias: %.2f Cache: %.1f/%.1f MB mVRAM: %d",
                    (S32)LLViewerTexture::sFreeVRAMMegabytes,
                    LLMemory::getAvailableMemKB()/1024,
                    LLImageGL::getTextureBytesAllocated() / 1024 / 1024,
                    LLRenderTarget::sBytesAllocated/(1024*1024),
                    gPipeline.mReflectionMapManager.probeCount(),
                    gPipeline.mReflectionMapManager.probeMemory(),
                    discard_bias,
                    cache_usage,
                    cache_max_usage,
                    gGLManager.mVRAM);
    // <FS:Ansariel> Texture memory bars
    //LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height*7,
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height*9,
    // </FS:Ansariel>
                                             text_color, LLFontGL::LEFT, LLFontGL::TOP);

    // <FS:Ansariel> Texture memory bars
    S32 bar_left = 0;
    constexpr S32 bar_width = 200;
    constexpr S32 bar_space = 10;
    S32 top = line_height*8 - 2 + v_offset;
    S32 bottom = top - 6;
    S32 left = bar_left;
    S32 right = left + bar_width;
    F32 bar_scale;

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // VRAM Mem Bar
    text = "VRAM";
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, left, v_offset + line_height*8,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);
    left += 35;
    right = left + bar_width;

    gGL.color4f(0.5f, 0.5f, 0.5f, 0.75f);
    gl_rect_2d(left, top, right, bottom);

    S32 gpu_used = gGLManager.mVRAM - (S32)LLViewerTexture::sFreeVRAMMegabytes;
    color = (gpu_used < llfloor(gGLManager.mVRAM * 0.85f)) ? LLColor4::green :
        (gpu_used < gGLManager.mVRAM) ? LLColor4::yellow : LLColor4::red;
    color[VALPHA] = .75f;

    bar_scale = (F32)bar_width / gGLManager.mVRAM;
    right = left + llfloor(gpu_used * bar_scale);

    gl_rect_2d(left, top, right, bottom, color);
    // </FS:Ansariel>
    // <FS:Beq> Texture cache bars
    bar_left = left + bar_width + bar_space;
    left = bar_left;
    // VRAM Mem Bar
    text = "CACHE";
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, left, v_offset + line_height*8,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    left += 35;
    right = left + bar_width;

    gGL.color4f(0.5f, 0.5f, 0.5f, 0.75f);
    gl_rect_2d(left, top, right, bottom);

    color = (cache_usage < cache_max_usage * 0.8f)? LLColor4::green :
        (cache_usage < cache_max_usage)? LLColor4::yellow : LLColor4::red;
    color[VALPHA] = .75f;

    bar_scale = (F32)bar_width / cache_max_usage;
    right = left + llfloor(cache_usage * bar_scale);

    gl_rect_2d(left, top, right, bottom, color);
    // </FS:Beq>

    text = llformat("Images: %d   Raw: %d (%.2f MB)  Saved: %d (%.2f MB) Aux: %d (%.2f MB)", image_count, raw_image_count, raw_image_bytes_MB,
        saved_raw_image_count, saved_raw_image_bytes_MB,
        aux_raw_image_count, aux_raw_image_bytes_MB);
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height * 7,
        text_color, LLFontGL::LEFT, LLFontGL::TOP);

    text = llformat("Textures: %.2f MB  Vertex: %.2f MB  Render: %.2f MB  Total: %.2f MB",
                    texture_bytes_alloc,
                    vertex_bytes_alloc,
                    render_bytes_alloc,
        texture_bytes_alloc+vertex_bytes_alloc);
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height * 6,
        text_color, LLFontGL::LEFT, LLFontGL::TOP);

    U32 cache_read(0U), cache_write(0U), res_wait(0U);
    LLAppViewer::getTextureFetch()->getStateStats(&cache_read, &cache_write, &res_wait);

    // <FS:Ansariel> Fast cache stats
    //text = llformat("Net Tot Tex: %.1f MB Tot Obj: %.1f MB #Objs/#Cached: %d/%d Tot Htp: %d Cread: %u Cwrite: %u Rwait: %u",
    text = llformat("Net Tot Tex: %.1f MB Tot Obj: %.1f MB #Objs/#Cached: %d/%d Tot Htp: %d Cread: %u Cwrite: %u Rwait: %u FCread: %u",
    // </FS:Ansariel>
                    total_texture_downloaded.valueInUnits<LLUnits::Megabytes>(),
                    total_object_downloaded.valueInUnits<LLUnits::Megabytes>(),
                    total_objects,
                    total_active_cached_objects,
                    total_http_requests,
                    cache_read,
                    cache_write,
                    // <FS:Ansariel> Fast cache stats
                    //res_wait);
                    res_wait,
                    LLViewerTextureList::sNumFastCacheReads);
                    // </FS:Ansariel>
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height*5,
                                             text_color, LLFontGL::LEFT, LLFontGL::TOP);

    text = llformat("CacheHitRate: %3.2f Read: %d/%d/%d Decode: %d/%d/%d Queue: %d Decoding: %d Fetch: %d/%d/%d",
                    cacheHitRate,
                    cacheReadLatMin,
                    cacheReadLatMed,
                    cacheReadLatMax,
                    texDecodeLatMin,
                    texDecodeLatMed,
                    texDecodeLatMax,
                    (S32)LLAppViewer::getImageDecodeThread()->getPending(),
                    (S32)gTextureList.aDecodingCount,
                    texFetchLatMin,
                    texFetchLatMed,
                    texFetchLatMax);

    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height*4,
                                             text_color, LLFontGL::LEFT, LLFontGL::TOP);

    //----------------------------------------------------------------------------

    // <FS:Ansariel> Fast cache stats
    //text = llformat("Textures: %d Fetch: %d(%d) Pkts:%d(%d) Cache R/W: %d/%d LFS:%d RAW:%d HTP:%d DEC:%d CRE:%d ",
    text = llformat("Tex: %d Fetch: %d(%d) Pkts:%d(%d) CAC R/W: %d/%d LFS:%d RAW:%d HTP:%d DEC:%d CRE:%d FCA:%d ",
    // </FS:Ansariel>
    // <FS:minerjr> Fixed up the missing variables and converted 64bit size_t's to S32's to allow proper numbers to appear
                    gTextureList.getNumImages(),
                    (S32)gTextureList.mFetchingTextures.size(), LLAppViewer::getTextureFetch()->getNumDeletes(),
                    LLAppViewer::getTextureFetch()->mPacketCount, LLAppViewer::getTextureFetch()->mBadPacketCount,
                    LLAppViewer::getTextureCache()->getNumReads(), LLAppViewer::getTextureCache()->getNumWrites(),
                    (S32)LLLFSThread::sLocal->getPending(),
                    (S32)LLImageRaw::sRawImageCount,
                    LLAppViewer::getTextureFetch()->getNumHTTPRequests(),
                    (S32)LLAppViewer::getImageDecodeThread()->getPending(),
                    // <FS:Ansariel> Fast cache stats
                    //gTextureList.mCreateTextureList.size());
                    (S32)gTextureList.mCreateTextureList.size(),
                    (S32)gTextureList.mFastCacheList.size());
                    // </FS:Ansariel>
    // </FS:minerjr>
    x_right = 550.0f;
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0.f, (F32)(v_offset + line_height*3),
                                             text_color, LLFontGL::LEFT, LLFontGL::TOP,
                                             LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, S32_MAX, &x_right);

    // <FS:Ansariel> Move BW figures further to the right to prevent overlapping
    left = 575;
    F32Kilobits bandwidth( LLAppViewer::getTextureFetch()->getTextureBandwidth() );
    // <FS:Ansariel> Speed-up
    //F32Kilobits max_bandwidth = gSavedSettings.getF32("ThrottleBandwidthKBPS");
    static LLCachedControl<F32> throttleBandwidthKBPS(gSavedSettings, "ThrottleBandwidthKBPS");
    F32Kilobits max_bandwidth( (F32)throttleBandwidthKBPS );
    // </FS:Ansariel> Speed-upx
    color = bandwidth.value() > max_bandwidth.value() ? LLColor4::red : bandwidth.value() > max_bandwidth.value() * .75f ? LLColor4::yellow : text_color;
    color[VALPHA] = text_color[VALPHA];
    text = llformat("BW:%.0f/%.0f",bandwidth.value(), max_bandwidth.value());
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, (S32)x_right, v_offset + line_height*3,
                                             color, LLFontGL::LEFT, LLFontGL::TOP);

    // Mesh status line
    text = llformat("Mesh: Reqs(Tot/Htp/Big): %u/%u/%u Rtr/Err: %u/%u Cread/Cwrite: %u/%u Low/At/High: %d/%d/%d",
                    LLMeshRepository::sMeshRequestCount, LLMeshRepository::sHTTPRequestCount, LLMeshRepository::sHTTPLargeRequestCount,
                    LLMeshRepository::sHTTPRetryCount, LLMeshRepository::sHTTPErrorCount,
                    (U32)LLMeshRepository::sCacheReads, (U32)LLMeshRepository::sCacheWrites,
                    LLMeshRepoThread::sRequestLowWater, LLMeshRepoThread::sRequestWaterLevel, LLMeshRepoThread::sRequestHighWater);
    LLFontGL::getFontMonospace()->renderUTF8(text, 0, 0, v_offset + line_height*2,
                                             text_color, LLFontGL::LEFT, LLFontGL::TOP);

    // Header for texture table columns
    S32 dx1 = 0;
    if (LLAppViewer::getTextureFetch()->mDebugPause)
    {
        LLFontGL::getFontMonospace()->renderUTF8(std::string("!"), 0, title_x1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
        dx1 += 8;
    }
    if (mTextureView->mFreezeView)
    {
        LLFontGL::getFontMonospace()->renderUTF8(std::string("*"), 0, title_x1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
        dx1 += 8;
    }
    if (mTextureView->mOrderFetch)
    {
        LLFontGL::getFontMonospace()->renderUTF8(title_string1b, 0, title_x1+dx1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
    }
    else
    {
        LLFontGL::getFontMonospace()->renderUTF8(title_string1a, 0, title_x1+dx1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
    }

    LLFontGL::getFontMonospace()->renderUTF8(title_string2, 0, title_x2, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    LLFontGL::getFontMonospace()->renderUTF8(title_string3, 0, title_x3, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    LLFontGL::getFontMonospace()->renderUTF8(title_string4, 0, title_x4, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);
}

bool LLGLTexMemBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    return false;
}

LLRect LLGLTexMemBar::getRequiredRect()
{
    LLRect rect;
    // <FS:Ansariel> Texture memory bars
    //rect.mTop = 78; //LLFontGL::getFontMonospace()->getLineHeight() * 6;
    rect.mTop = 93;
    // </FS:Ansariel>
    return rect;
}

////////////////////////////////////////////////////////////////////////////
class LLGLTexSizeBar
{
public:
    LLGLTexSizeBar(S32 index, S32 left, S32 bottom, S32 right, S32 line_height)
    {
        mIndex = index ;
        mLeft = left ;
        mBottom = bottom ;
        mRight = right ;
        mLineHeight = line_height ;
        mTopLoaded = 0 ;
        mTopBound = 0 ;
        mScale = 1.0f ;
    }

    void setTop(S32 loaded, S32 bound, F32 scale) {mTopLoaded = loaded ; mTopBound = bound; mScale = scale ;}

    void draw();
    bool handleHover(S32 x, S32 y, MASK mask, bool set_pick_size) ;

private:
    S32 mIndex ;
    S32 mLeft ;
    S32 mBottom ;
    S32 mRight ;
    S32 mTopLoaded ;
    S32 mTopBound ;
    S32 mLineHeight ;
    F32 mScale ;
};

bool LLGLTexSizeBar::handleHover(S32 x, S32 y, MASK mask, bool set_pick_size)
{
    if(y > mBottom && (y < mBottom + (S32)(mTopLoaded * mScale) || y < mBottom + (S32)(mTopBound * mScale)))
    {
        LLImageGL::setCurTexSizebar(mIndex, set_pick_size);
    }
    return true ;
}
void LLGLTexSizeBar::draw()
{
    LLGLSUIDefault gls_ui;

    if(LLImageGL::sCurTexSizeBar == mIndex)
    {
        LLColor4 text_color(1.f, 1.f, 1.f, 0.75f);
        std::string text;

        text = llformat("%d", mTopLoaded) ;
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, mLeft, mBottom + (S32)(mTopLoaded * mScale) + mLineHeight,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

        text = llformat("%d", mTopBound) ;
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, (mLeft + mRight) / 2, mBottom + (S32)(mTopBound * mScale) + mLineHeight,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);
    }

    LLColor4 loaded_color(1.0f, 0.0f, 0.0f, 0.75f);
    LLColor4 bound_color(1.0f, 1.0f, 0.0f, 0.75f);
    gl_rect_2d(mLeft, mBottom + (S32)(mTopLoaded * mScale), (mLeft + mRight) / 2, mBottom, loaded_color) ;
    gl_rect_2d((mLeft + mRight) / 2, mBottom + (S32)(mTopBound * mScale), mRight, mBottom, bound_color) ;
}
////////////////////////////////////////////////////////////////////////////

LLTextureView::LLTextureView(const LLTextureView::Params& p)
    :   LLContainerView(p),
        mFreezeView(false),
        mOrderFetch(false),
        mPrintList(false),
        mNumTextureBars(0)
{
    setVisible(false);

    setDisplayChildren(true);
    mGLTexMemBar = 0;
    mAvatarTexBar = 0;
}

LLTextureView::~LLTextureView()
{
    // Children all cleaned up by default view destructor.
    delete mGLTexMemBar;
    mGLTexMemBar = 0;

    delete mAvatarTexBar;
    mAvatarTexBar = 0;
}

typedef std::pair<F32,LLViewerFetchedTexture*> decode_pair_t;
struct compare_decode_pair
{
    bool operator()(const decode_pair_t& a, const decode_pair_t& b) const
    {
        return a.first > b.first;
    }
};

struct KillView
{
    void operator()(LLView* viewp)
    {
        viewp->getParent()->removeChild(viewp);
        viewp->die();
    }
};

void LLTextureView::draw()
{
    if (!mFreezeView)
    {
//      LLViewerObject *objectp;
//      S32 te;

        for_each(mTextureBars.begin(), mTextureBars.end(), KillView());
        mTextureBars.clear();

        if (mGLTexMemBar)
        {
            removeChild(mGLTexMemBar);
            mGLTexMemBar->die();
            mGLTexMemBar = 0;
        }

        if (mAvatarTexBar)
        {
            removeChild(mAvatarTexBar);
            mAvatarTexBar->die();
            mAvatarTexBar = 0;
        }

        typedef std::multiset<decode_pair_t, compare_decode_pair > display_list_t;
        display_list_t display_image_list;

        if (mPrintList)
        {
            LL_INFOS() << "ID\tMEM\tBOOST\tPRI\tWIDTH\tHEIGHT\tDISCARD" << LL_ENDL;
        }

        for (LLViewerTextureList::image_list_t::iterator iter = gTextureList.mImageList.begin();
             iter != gTextureList.mImageList.end(); )
        {
            LLViewerFetchedTexture* imagep = *iter++;
            if(!imagep->hasFetcher())
            {
                continue ;
            }

            S32 cur_discard = imagep->getDiscardLevel();
            S32 desired_discard = imagep->mDesiredDiscardLevel;

            if (mPrintList)
            {
                S32 tex_mem = imagep->hasGLTexture() ? imagep->getTextureMemory().value() : 0 ;
                LL_INFOS() << imagep->getID()
                        << "\t" << tex_mem
                        << "\t" << imagep->getBoostLevel()
                        << "\t" << imagep->getMaxVirtualSize()
                        << "\t" << imagep->getWidth()
                        << "\t" << imagep->getHeight()
                        << "\t" << cur_discard
                        << LL_ENDL;
            }

            if (imagep->getID() == LLAppViewer::getTextureFetch()->mDebugID)
            {
//              static S32 debug_count = 0;
//              ++debug_count; // for breakpoints
            }

            F32 pri;
            if (mOrderFetch)
            {
                pri = ((F32)imagep->mFetchPriority)/256.f;
            }
            else
            {
                pri = imagep->getMaxVirtualSize();
            }
            pri = llclamp(pri, 0.0f, HIGH_PRIORITY-1.f);

            if (sDebugImages.find(imagep) != sDebugImages.end())
            {
                pri += 4*HIGH_PRIORITY;
            }

            if (!mOrderFetch)
            {
                if (pri < HIGH_PRIORITY && LLSelectMgr::getInstance())
                {
                    struct f : public LLSelectedTEFunctor
                    {
                        LLViewerFetchedTexture* mImage;
                        f(LLViewerFetchedTexture* image) : mImage(image) {}
                        virtual bool apply(LLViewerObject* object, S32 te)
                        {
                            return (mImage == object->getTEImage(te));
                        }
                    } func(imagep);
                    const bool firstonly = true;
                    bool match = LLSelectMgr::getInstance()->getSelection()->applyToTEs(&func, firstonly);
                    if (match)
                    {
                        pri += 3*HIGH_PRIORITY;
                    }
                }

                if (pri < HIGH_PRIORITY && (cur_discard< 0 || desired_discard < cur_discard))
                {
                    LLSelectNode* hover_node = LLSelectMgr::instance().getHoverNode();
                    if (hover_node)
                    {
                        LLViewerObject *objectp = hover_node->getObject();
                        if (objectp)
                        {
                            S32 tex_count = objectp->getNumTEs();
                            for (S32 i = 0; i < tex_count; i++)
                            {
                                if (imagep == objectp->getTEImage(i))
                                {
                                    pri += 2*HIGH_PRIORITY;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (pri > 0.f && pri < HIGH_PRIORITY)
                {
                    if (imagep->mLastPacketTimer.getElapsedTimeF32() < 1.f ||
                        imagep->mFetchDeltaTime < 0.25f)
                    {
                        pri += 1*HIGH_PRIORITY;
                    }
                }
            }

            if (pri > 0.0f)
            {
                display_image_list.insert(std::make_pair(pri, imagep));
            }
        }

        if (mPrintList)
        {
            mPrintList = false;
        }

        static S32 max_count = 50;
        S32 count = 0;
        mNumTextureBars = 0 ;
        for (display_list_t::iterator iter = display_image_list.begin();
             iter != display_image_list.end(); iter++)
        {
            LLViewerFetchedTexture* imagep = iter->second;
            S32 hilite = 0;
            F32 pri = iter->first;
            if (pri >= 1 * HIGH_PRIORITY)
            {
                hilite = (S32)((pri+1) / HIGH_PRIORITY) - 1;
            }
            if ((hilite || count < max_count-10) && (count < max_count))
            {
                if (addBar(imagep, hilite))
                {
                    count++;
                }
            }
        }

        if (mOrderFetch)
            sortChildren(LLTextureBar::sort_fetch());
        else
            sortChildren(LLTextureBar::sort());

        LLGLTexMemBar::Params tmbp;
        LLRect tmbr;
        tmbp.name("gl texmem bar");
        tmbp.rect(tmbr);
        tmbp.follows.flags = FOLLOWS_LEFT|FOLLOWS_TOP;
        tmbp.texture_view(this);
        mGLTexMemBar = LLUICtrlFactory::create<LLGLTexMemBar>(tmbp);
        addChild(mGLTexMemBar);
        sendChildToFront(mGLTexMemBar);

        LLAvatarTexBar::Params atbp;
        LLRect atbr;
        atbp.name("gl avatartex bar");
        atbp.texture_view(this);
        atbp.rect(atbr);
        mAvatarTexBar = LLUICtrlFactory::create<LLAvatarTexBar>(atbp);
        addChild(mAvatarTexBar);
        sendChildToFront(mAvatarTexBar);

        reshape(getRect().getWidth(), getRect().getHeight(), true);

        LLUI::popMatrix();
        LLUI::pushMatrix();
        LLUI::translate((F32)getRect().mLeft, (F32)getRect().mBottom);

        for (child_list_const_iter_t child_iter = getChildList()->begin();
             child_iter != getChildList()->end(); ++child_iter)
        {
            LLView *viewp = *child_iter;
            if (viewp->getRect().mBottom < 0)
            {
                viewp->setVisible(false);
            }
        }
    }

    LLContainerView::draw();

}

bool LLTextureView::addBar(LLViewerFetchedTexture *imagep, S32 hilite)
{
    llassert(imagep);

    LLTextureBar *barp;
    LLRect r;

    mNumTextureBars++;

    LLTextureBar::Params tbp;
    tbp.name("texture bar");
    tbp.rect(r);
    tbp.texture_view(this);
    barp = LLUICtrlFactory::create<LLTextureBar>(tbp);
    barp->mImagep = imagep;
    barp->mHilite = hilite;

    addChild(barp);
    mTextureBars.push_back(barp);

    return true;
}

bool LLTextureView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == (MASK_ALT|MASK_SHIFT))
    {
        mPrintList = true;
        return true;
    }
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == (MASK_CONTROL|MASK_SHIFT))
    {
        LLAppViewer::getTextureFetch()->mDebugPause = !LLAppViewer::getTextureFetch()->mDebugPause;
        return true;
    }
    if (mask & MASK_SHIFT)
    {
        mFreezeView = !mFreezeView;
        return true;
    }
    if (mask & MASK_CONTROL)
    {
        mOrderFetch = !mOrderFetch;
        return true;
    }
    return LLView::handleMouseDown(x,y,mask);
}

bool LLTextureView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    return false;
}

bool LLTextureView::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    return false;
}


