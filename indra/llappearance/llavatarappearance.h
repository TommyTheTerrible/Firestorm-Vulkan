/**
 * @file llavatarappearance.h
 * @brief Declaration of LLAvatarAppearance class
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
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

#ifndef LL_AVATAR_APPEARANCE_H
#define LL_AVATAR_APPEARANCE_H

#include "llcharacter.h"
#include "llavatarappearancedefines.h"
#include "llavatarjointmesh.h"
#include "lldriverparam.h"
#include "lltexlayer.h"
#include "llviewervisualparam.h"
#include "llxmltree.h"

class LLTexLayerSet;
class LLTexGlobalColor;
class LLTexGlobalColorInfo;
class LLWearableData;
class LLAvatarBoneInfo;
class LLAvatarSkeletonInfo;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLAvatarAppearance
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLAvatarAppearance : public LLCharacter
{
    LOG_CLASS(LLAvatarAppearance);

protected:
    struct LLAvatarXmlInfo;

/********************************************************************************
 **                                                                            **
 **                    INITIALIZATION
 **/
private:
    // Hide default constructor.
    LLAvatarAppearance() {}

public:
    LLAvatarAppearance(LLWearableData* wearable_data);
    virtual ~LLAvatarAppearance();

    static void         initClass(const std::string& avatar_file_name, const std::string& skeleton_file_name); // initializes static members
    static void         initClass();
    static void         cleanupClass(); // Cleanup data that's only init'd once per class.
    virtual void        initInstance(); // Called after construction to initialize the instance.
    S32                 mInitFlags{ 0 };
    virtual bool        loadSkeletonNode();
    bool                loadMeshNodes();
    bool                loadLayersets();


/**                    Initialization
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    INHERITED
 **/

    //--------------------------------------------------------------------
    // LLCharacter interface and related
    //--------------------------------------------------------------------
public:
    /*virtual*/ LLJoint*        getCharacterJoint(U32 num);

    /*virtual*/ const char*     getAnimationPrefix() { return "avatar"; }
    /*virtual*/ LLVector3       getVolumePos(S32 joint_index, LLVector3& volume_offset);
    /*virtual*/ LLJoint*        findCollisionVolume(S32 volume_id);
    /*virtual*/ S32             getCollisionVolumeID(std::string &name);
    /*virtual*/ LLPolyMesh*     getHeadMesh();
    /*virtual*/ LLPolyMesh*     getUpperBodyMesh();

/**                    Inherited
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    STATE
 **/
public:
    virtual bool    isSelf() const { return false; } // True if this avatar is for this viewer's agent
    virtual bool    isValid() const;
    virtual bool    isUsingLocalAppearance() const = 0;
    virtual bool    isEditingAppearance() const = 0;

    bool isBuilt() const { return mIsBuilt; }

    // <FS:Ansariel> [Legacy Bake]
    virtual bool    isUsingServerBakes() const = 0;

/**                    State
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    SKELETON
 **/

protected:
    virtual LLAvatarJoint*  createAvatarJoint() = 0;
    virtual LLAvatarJoint*  createAvatarJoint(S32 joint_num) = 0;
    virtual LLAvatarJointMesh*  createAvatarJointMesh() = 0;
    void makeJointAliases(LLAvatarBoneInfo *bone_info);


public:
    F32                 getPelvisToFoot() const { return mPelvisToFoot; }
    /*virtual*/ LLJoint*    getRootJoint() { return mRoot; }

    LLVector3           mHeadOffset{}; // current head position
    LLAvatarJoint*      mRoot{ nullptr };

    //<FS:Ansariel> Joint-lookup improvements
    // typedef std::map<std::string, LLJoint*> joint_map_t;
    typedef std::map<std::string, LLJoint*, std::less<>> joint_map_t;

    joint_map_t         mJointMap;

    typedef std::map<std::string, LLVector3> joint_state_map_t;
    joint_state_map_t mLastBodySizeState;
    joint_state_map_t mCurrBodySizeState;
    void compareJointStateMaps(joint_state_map_t& last_state,
                               joint_state_map_t& curr_state);
    void        computeBodySize();

public:
    typedef std::vector<LLAvatarJoint*> avatar_joint_list_t;
    const avatar_joint_list_t& getSkeleton() { return mSkeleton; }
    typedef std::map<std::string, std::string, std::less<>> joint_alias_map_t;
    const joint_alias_map_t& getJointAliases();


protected:
    static bool         parseSkeletonFile(const std::string& filename, LLXmlTree& skeleton_xml_tree);
    virtual void        buildCharacter();
    virtual bool        loadAvatar();
// [RLVa:KB] - Checked: 2013-03-03 (RLVa-1.4.8)
    virtual F32         getAvatarOffset() /*const*/;
// [/RLVa:KB]
    // <FS:Ansariel> [Legacy Bake]
    virtual void        bodySizeChanged() = 0;

    bool                setupBone(const LLAvatarBoneInfo* info, LLJoint* parent, S32 &current_volume_num, S32 &current_joint_num);
    bool                allocateCharacterJoints(S32 num);
    bool                buildSkeleton(const LLAvatarSkeletonInfo *info);

    void                clearSkeleton();
    bool                mIsBuilt{ false }; // state of deferred character building
    avatar_joint_list_t mSkeleton;
    LLVector3OverrideMap    mPelvisFixups;
    joint_alias_map_t   mJointAliasMap;

    //--------------------------------------------------------------------
    // Pelvis height adjustment members.
    //--------------------------------------------------------------------
public:
    void                addPelvisFixup( F32 fixup, const LLUUID& mesh_id );
    void                removePelvisFixup( const LLUUID& mesh_id );
    bool                hasPelvisFixup( F32& fixup, LLUUID& mesh_id ) const;
    bool                hasPelvisFixup( F32& fixup ) const;

    LLVector3           mBodySize;
    LLVector3           mAvatarOffset;
protected:
    F32                 mPelvisToFoot{ 0.f };

    //--------------------------------------------------------------------
    // Cached pointers to well known joints
    //--------------------------------------------------------------------
public:
    LLJoint*        mPelvisp;
    LLJoint*        mTorsop;
    LLJoint*        mChestp;
    LLJoint*        mNeckp;
    LLJoint*        mHeadp;
    LLJoint*        mSkullp;
    LLJoint*        mEyeLeftp;
    LLJoint*        mEyeRightp;
    LLJoint*        mHipLeftp;
    LLJoint*        mHipRightp;
    LLJoint*        mKneeLeftp;
    LLJoint*        mKneeRightp;
    LLJoint*        mAnkleLeftp;
    LLJoint*        mAnkleRightp;
    LLJoint*        mFootLeftp;
    LLJoint*        mFootRightp;
    LLJoint*        mWristLeftp;
    LLJoint*        mWristRightp;

    //--------------------------------------------------------------------
    // XML parse tree
    //--------------------------------------------------------------------
protected:
    static LLAvatarSkeletonInfo*                    sAvatarSkeletonInfo;
    static LLAvatarXmlInfo*                         sAvatarXmlInfo;

// <FS:Ansariel> Get attachment point name from ID
public:
    static std::string getAttachmentPointName(S32 attachmentPointId);
// </FS:Ansariel>

/**                    Skeleton
 **                                                                            **
 *******************************************************************************/


/********************************************************************************
 **                                                                            **
 **                    RENDERING
 **/
public:
    bool        mIsDummy{ false }; // for special views and animated object controllers; local to viewer

    //--------------------------------------------------------------------
    // Morph masks
    //--------------------------------------------------------------------
public:
    void    addMaskedMorph(LLAvatarAppearanceDefines::EBakedTextureIndex index, LLVisualParam* morph_target, bool invert, std::string layer);
    virtual void    applyMorphMask(const U8* tex_data, S32 width, S32 height, S32 num_components, LLAvatarAppearanceDefines::EBakedTextureIndex index = LLAvatarAppearanceDefines::BAKED_NUM_INDICES) = 0;

/**                    Rendering
 **                                                                            **
 *******************************************************************************/

    //--------------------------------------------------------------------
    // Composites
    //--------------------------------------------------------------------
public:
    // <FS:Ansariel> [Legacy Bake]
    //virtual void  invalidateComposite(LLTexLayerSet* layerset) = 0;
    virtual void    invalidateComposite(LLTexLayerSet* layerset, bool upload_result) = 0;

/********************************************************************************
 **                                                                            **
 **                    MESHES
 **/

public:
    virtual void    updateMeshTextures() = 0;
    virtual void    dirtyMesh() = 0; // Dirty the avatar mesh
    static const LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary *getDictionary() { return sAvatarDictionary; }
protected:
    virtual void    dirtyMesh(S32 priority) = 0; // Dirty the avatar mesh, with priority

protected:
    typedef std::multimap<std::string, LLPolyMesh*> polymesh_map_t;
    polymesh_map_t                                  mPolyMeshes;
    avatar_joint_list_t                             mMeshLOD;

    // mesh entries and backed textures
    static LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary* sAvatarDictionary;

/**                    Meshes
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    APPEARANCE
 **/

    //--------------------------------------------------------------------
    // Clothing colors (convenience functions to access visual parameters)
    //--------------------------------------------------------------------
public:
    // <FS:Ansariel> [Legacy Bake]
    //void          setClothesColor(LLAvatarAppearanceDefines::ETextureIndex te, const LLColor4& new_color);
    void            setClothesColor(LLAvatarAppearanceDefines::ETextureIndex te, const LLColor4& new_color, bool upload_bake);
    // </FS:Ansariel> [Legacy Bake]
    LLColor4        getClothesColor(LLAvatarAppearanceDefines::ETextureIndex te);
    static bool     teToColorParams(LLAvatarAppearanceDefines::ETextureIndex te, U32 *param_name);

    //--------------------------------------------------------------------
    // Global colors
    //--------------------------------------------------------------------
public:
    LLColor4        getGlobalColor(const std::string& color_name ) const;
    // <FS:Ansariel> [Legacy Bake]
    //virtual void  onGlobalColorChanged(const LLTexGlobalColor* global_color) = 0;
    virtual void    onGlobalColorChanged(const LLTexGlobalColor* global_color, bool upload_bake) = 0;
    // </FS:Ansariel> [Legacy Bake]
protected:
    LLTexGlobalColor* mTexSkinColor{ nullptr };
    LLTexGlobalColor* mTexHairColor{ nullptr };
    LLTexGlobalColor* mTexEyeColor{ nullptr };

    //--------------------------------------------------------------------
    // Visibility
    //--------------------------------------------------------------------
public:
    static LLColor4 getDummyColor();
/**                    Appearance
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    WEARABLES
 **/

public:
    LLWearableData*         getWearableData() { return mWearableData; }
    const LLWearableData*   getWearableData() const { return mWearableData; }
    virtual bool isTextureDefined(LLAvatarAppearanceDefines::ETextureIndex te, U32 index = 0 ) const = 0;
    virtual bool            isWearingWearableType(LLWearableType::EType type ) const;

private:
    LLWearableData* mWearableData{ nullptr };

/********************************************************************************
 **                                                                            **
 **                    BAKED TEXTURES
 **/
public:
    LLTexLayerSet*      getAvatarLayerSet(LLAvatarAppearanceDefines::EBakedTextureIndex baked_index) const;

protected:
    virtual LLTexLayerSet*  createTexLayerSet() = 0;

protected:
    class LLMaskedMorph;
    typedef std::deque<LLMaskedMorph *>     morph_list_t;
    struct BakedTextureData
    {
        LLUUID                              mLastTextureID;
        LLTexLayerSet*                      mTexLayerSet{ nullptr }; // Only exists for self
        bool                                mIsLoaded{ false };
        bool                                mIsUsed{ false };
        LLAvatarAppearanceDefines::ETextureIndex    mTextureIndex{ LLAvatarAppearanceDefines::ETextureIndex::TEX_INVALID };
        U32                                 mMaskTexName{ 0 };
        // Stores pointers to the joint meshes that this baked texture deals with
        avatar_joint_mesh_list_t            mJointMeshes;
        morph_list_t                        mMaskedMorphs;
    };
    typedef std::vector<BakedTextureData>   bakedtexturedata_vec_t;
    bakedtexturedata_vec_t                  mBakedTextureDatas;

/********************************************************************************
 **                                                                            **
 **                    PHYSICS
 **/

    //--------------------------------------------------------------------
    // Collision volumes
    //--------------------------------------------------------------------
public:
    S32         mNumBones{ 0 };
    S32         mNumCollisionVolumes{ 0 };
    LLAvatarJointCollisionVolume* mCollisionVolumes{ nullptr };
protected:
    bool        allocateCollisionVolumes(U32 num);

/**                    Physics
 **                                                                            **
 *******************************************************************************/

/********************************************************************************
 **                                                                            **
 **                    SUPPORT CLASSES
 **/

    struct LLAvatarXmlInfo
    {
        LLAvatarXmlInfo();
        ~LLAvatarXmlInfo();

        bool    parseXmlSkeletonNode(LLXmlTreeNode* root);
        bool    parseXmlMeshNodes(LLXmlTreeNode* root);
        bool    parseXmlColorNodes(LLXmlTreeNode* root);
        bool    parseXmlLayerNodes(LLXmlTreeNode* root);
        bool    parseXmlDriverNodes(LLXmlTreeNode* root);
        bool    parseXmlMorphNodes(LLXmlTreeNode* root);

        struct LLAvatarMeshInfo
        {
            typedef std::pair<LLViewerVisualParamInfo*,bool> morph_info_pair_t; // LLPolyMorphTargetInfo stored here
            typedef std::vector<morph_info_pair_t> morph_info_list_t;

            LLAvatarMeshInfo() : mLOD(0), mMinPixelArea(.1f) {}
            ~LLAvatarMeshInfo()
            {
                for (morph_info_list_t::value_type& pair : mPolyMorphTargetInfoList)
                {
                    delete pair.first;
                }
                mPolyMorphTargetInfoList.clear();
            }

            std::string mType;
            S32         mLOD;
            std::string mMeshFileName;
            std::string mReferenceMeshName;
            F32         mMinPixelArea;
            morph_info_list_t mPolyMorphTargetInfoList;
        };
        typedef std::vector<LLAvatarMeshInfo*> mesh_info_list_t;
        mesh_info_list_t mMeshInfoList;

        typedef std::vector<LLViewerVisualParamInfo*> skeletal_distortion_info_list_t; // LLPolySkeletalDistortionInfo stored here
        skeletal_distortion_info_list_t mSkeletalDistortionInfoList;

        struct LLAvatarAttachmentInfo
        {
            LLAvatarAttachmentInfo()
                : mGroup(-1), mAttachmentID(-1), mPieMenuSlice(-1), mVisibleFirstPerson(false),
                  mIsHUDAttachment(false), mHasPosition(false), mHasRotation(false) {}
            std::string mName;
            std::string mJointName;
            LLVector3 mPosition;
            LLVector3 mRotationEuler;
            S32 mGroup;
            S32 mAttachmentID;
            S32 mPieMenuSlice;
            bool mVisibleFirstPerson;
            bool mIsHUDAttachment;
            bool mHasPosition;
            bool mHasRotation;
        };
        typedef std::vector<LLAvatarAttachmentInfo*> attachment_info_list_t;
        attachment_info_list_t mAttachmentInfoList;

        LLTexGlobalColorInfo *mTexSkinColorInfo;
        LLTexGlobalColorInfo *mTexHairColorInfo;
        LLTexGlobalColorInfo *mTexEyeColorInfo;

        typedef std::vector<LLTexLayerSetInfo*> layer_info_list_t;
        layer_info_list_t mLayerInfoList;

        typedef std::vector<LLDriverParamInfo*> driver_info_list_t;
        driver_info_list_t mDriverInfoList;

        struct LLAvatarMorphInfo
        {
            LLAvatarMorphInfo()
                : mInvert(false) {}
            std::string mName;
            std::string mRegion;
            std::string mLayer;
            bool mInvert;
        };

        typedef std::vector<LLAvatarMorphInfo*> morph_info_list_t;
        morph_info_list_t   mMorphMaskInfoList;
    };


    class LLMaskedMorph
    {
    public:
        LLMaskedMorph(LLVisualParam *morph_target, bool invert, std::string layer);

        LLVisualParam   *mMorphTarget;
        bool                mInvert;
        std::string         mLayer;
    };
/**                    Support Classes
 **                                                                            **
 *******************************************************************************/
};

#endif // LL_AVATAR_APPEARANCE_H
