/**
 * @file llagentcamera.cpp
 * @brief LLAgent class implementation
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
#include "llagentcamera.h"

#include "pipeline.h"

#include "aoengine.h"           // ## Zi: Animation Overrider
#include "llagent.h"
#include "llanimationstates.h"
#include "llfloatercamera.h"
#include "llfloaterreg.h"
#include "llhudmanager.h"
#include "lljoystickbutton.h"
#include "llmorphview.h"
#include "llmoveview.h"
#include "llselectmgr.h"
#include "llsmoothstep.h"
#include "lltoolmgr.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerjoystick.h"
#include "llviewermenu.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llwindow.h"
#include "llworld.h"
// [RLVa:KB] - Checked: 2010-05-10 (RLVa-1.2.0g)
#include "rlvactions.h"
#include "rlvhandler.h"
// [/RLVa:KB]
#include "fscommon.h"
#include "lltrans.h"

using namespace LLAvatarAppearanceDefines;

extern LLMenuBarGL* gMenuBarView;

// Mousewheel camera zoom
const F32 MIN_ZOOM_FRACTION = 0.25f;
const F32 INITIAL_ZOOM_FRACTION = 1.f;
const F32 MAX_ZOOM_FRACTION = 8.f;

const F32 CAMERA_ZOOM_HALF_LIFE = 0.07f;    // seconds
const F32 FOV_ZOOM_HALF_LIFE = 0.07f;   // seconds

const F32 CAMERA_FOCUS_HALF_LIFE = 0.f;//0.02f;
const F32 CAMERA_LAG_HALF_LIFE = 0.25f;
const F32 MIN_CAMERA_LAG = 0.5f;
const F32 MAX_CAMERA_LAG = 5.f;

const F32 CAMERA_COLLIDE_EPSILON = 0.1f;
const F32 MIN_CAMERA_DISTANCE = 0.1f;

const F32 AVATAR_ZOOM_MIN_X_FACTOR = 0.55f;
const F32 AVATAR_ZOOM_MIN_Y_FACTOR = 0.7f;
const F32 AVATAR_ZOOM_MIN_Z_FACTOR = 1.15f;

const F32 MAX_CAMERA_DISTANCE_FROM_AGENT = 50.f;
const F32 MAX_CAMERA_DISTANCE_FROM_OBJECT = 496.f;
const F32 CAMERA_FUDGE_FROM_OBJECT = 16.f;

const F32 MAX_CAMERA_SMOOTH_DISTANCE = 50.0f;

const F32 HEAD_BUFFER_SIZE = 0.3f;

const F32 CUSTOMIZE_AVATAR_CAMERA_ANIM_SLOP = 0.1f;

const F32 LAND_MIN_ZOOM = 0.15f;

const F32 AVATAR_MIN_ZOOM = 0.5f;
const F32 OBJECT_MIN_ZOOM = 0.02f;

const F32 APPEARANCE_MIN_ZOOM = 0.39f;
const F32 APPEARANCE_MAX_ZOOM = 8.f;

const F32 CUSTOMIZE_AVATAR_CAMERA_DEFAULT_DIST = 3.5f;

const F32 GROUND_TO_AIR_CAMERA_TRANSITION_TIME = 0.5f;
const F32 GROUND_TO_AIR_CAMERA_TRANSITION_START_TIME = 0.5f;

const F32 OBJECT_EXTENTS_PADDING = 0.5f;

static bool isDisableCameraConstraints()
{
    static LLCachedControl<bool> sDisableCameraConstraints(gSavedSettings, "DisableCameraConstraints", false);
    return sDisableCameraConstraints;
}

// The agent instance.
LLAgentCamera gAgentCamera;

//-----------------------------------------------------------------------------
// LLAgentCamera()
//-----------------------------------------------------------------------------
LLAgentCamera::LLAgentCamera() :
    mInitialized(false),

    mDrawDistance( DEFAULT_FAR_PLANE ),

    mLookAt(NULL),
    mPointAt(NULL),

    mHUDTargetZoom(1.f),
    mHUDCurZoom(1.f),

    mForceMouselook(false),

    mCameraMode( CAMERA_MODE_THIRD_PERSON ),
    mLastCameraMode( CAMERA_MODE_THIRD_PERSON ),

    mCameraPreset(CAMERA_PRESET_REAR_VIEW),

    mCameraAnimating( false ),
    mAnimationCameraStartGlobal(),
    mAnimationFocusStartGlobal(),
    mAnimationTimer(),
    mAnimationDuration(0.33f),

    mCameraFOVZoomFactor(0.f),
    mCameraCurrentFOVZoomFactor(0.f),
    mCameraFocusOffset(),

    mCameraCollidePlane(),

    mCurrentCameraDistance(2.f),        // meters, set in init()
    mTargetCameraDistance(2.f),
    mCameraZoomFraction(1.f),           // deprecated
    mThirdPersonHeadOffset(0.f, 0.f, 1.f),
    mSitCameraEnabled(false),
    mCameraSmoothingLastPositionGlobal(),
    mCameraSmoothingLastPositionAgent(),
    mCameraSmoothingStop(false),

    mCameraUpVector(LLVector3::z_axis), // default is straight up

    mFocusOnAvatar(true),
    mAllowChangeToFollow(false),
    mFocusGlobal(),
    mFocusTargetGlobal(),
    mFocusObject(NULL),
    mFocusObjectDist(0.f),
    mFocusObjectOffset(),
    mTrackFocusObject(true),

    mAtKey(0), // Either 1, 0, or -1... indicates that movement-key is pressed
    mWalkKey(0), // like AtKey, but causes less forward thrust
    mLeftKey(0),
    mUpKey(0),
    mYawKey(0.f),
    mPitchKey(0.f),

    mOrbitLeftKey(0.f),
    mOrbitRightKey(0.f),
    mOrbitUpKey(0.f),
    mOrbitDownKey(0.f),
    mOrbitInKey(0.f),
    mOrbitOutKey(0.f),

    mPanUpKey(0.f),
    mPanDownKey(0.f),
    mPanLeftKey(0.f),
    mPanRightKey(0.f),
    mPanInKey(0.f),
    mPanOutKey(0.f),
// <FS:Chanayane> Camera roll (from Alchemy)
    mRollLeftKey(0.f),
    mRollRightKey(0.f),
// </FS:Chanayane>
    mPointAtObject(NULL)
{
    mFollowCam.setMaxCameraDistantFromSubject( MAX_CAMERA_DISTANCE_FROM_AGENT );

    clearGeneralKeys();
    clearOrbitKeys();
    clearPanKeys();

    resetPanDiff();
    resetOrbitDiff();
// <FS:Chanayane> Camera roll (from Alchemy)
	resetCameraRoll();
// </FS:Chanayane>
}

// Requires gSavedSettings to be initialized.
//-----------------------------------------------------------------------------
// init()
//-----------------------------------------------------------------------------
void LLAgentCamera::init()
{
    // *Note: this is where LLViewerCamera::getInstance() used to be constructed.

    mDrawDistance = gSavedSettings.getF32("RenderFarClip");

    LLViewerCamera::getInstance()->setView(DEFAULT_FIELD_OF_VIEW);
    // Leave at 0.1 meters until we have real near clip management
    LLViewerCamera::getInstance()->setNear(0.1f);
    LLViewerCamera::getInstance()->setFar(mDrawDistance);           // if you want to change camera settings, do so in camera.h
    LLViewerCamera::getInstance()->setAspect( gViewerWindow->getWorldViewAspectRatio() );       // default, overridden in LLViewerWindow::reshape
    LLViewerCamera::getInstance()->setViewHeightInPixels(768);          // default, overridden in LLViewerWindow::reshape

    mCameraFocusOffsetTarget = LLVector4(gSavedSettings.getVector3("CameraOffsetBuild"));

    mCameraPreset = (ECameraPreset) gSavedSettings.getU32("CameraPresetType");

// [RLVa:KB] - @setcam_eyeoffset, @setcam_focusoffset and @setcam_eyeoffsetscale
    if (RlvActions::isRlvEnabled())
    {
        mRlvCameraOffsetInitialControl = gSavedSettings.declareVec3("CameraOffsetRLVaView", LLVector3::zero, "Declared in code", LLControlVariable::PERSIST_NO);
        mRlvCameraOffsetInitialControl->setHiddenFromSettingsEditor(true);
        mRlvCameraOffsetScaleControl = gSavedSettings.declareF32("CameraOffsetScaleRLVa", 0.0f, "Declared in code", LLControlVariable::PERSIST_NO);
        mRlvCameraOffsetScaleControl->setHiddenFromSettingsEditor(true);
        mRlvFocusOffsetInitialControl = gSavedSettings.declareVec3d("FocusOffsetRLVaView", LLVector3d::zero, "Declared in code", LLControlVariable::PERSIST_NO);
        mRlvFocusOffsetInitialControl->setHiddenFromSettingsEditor(true);
    }
// [/RLVa:KB]

    mCameraCollidePlane.clearVec();
    mCurrentCameraDistance = getCameraOffsetInitial().magVec() * gSavedSettings.getF32("CameraOffsetScale");
    mTargetCameraDistance = mCurrentCameraDistance;
    mCameraZoomFraction = 1.f;
    mTrackFocusObject = gSavedSettings.getBOOL("TrackFocusObject");

    mInitialized = true;
}

//-----------------------------------------------------------------------------
// cleanup()
//-----------------------------------------------------------------------------
void LLAgentCamera::cleanup()
{
    setSitCamera(LLUUID::null);

    if(mLookAt)
    {
        mLookAt->markDead() ;
        mLookAt = NULL;
    }
    if(mPointAt)
    {
        mPointAt->markDead() ;
        mPointAt = NULL;
    }
    setFocusObject(NULL);
}

void LLAgentCamera::setAvatarObject(LLVOAvatarSelf* avatar)
{
    if (!mLookAt)
    {
        mLookAt = (LLHUDEffectLookAt *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_LOOKAT);
    }
    if (!mPointAt)
    {
        mPointAt = (LLHUDEffectPointAt *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_POINTAT);
    }

    if (!mLookAt.isNull())
    {
        mLookAt->setSourceObject(avatar);
    }
    if (!mPointAt.isNull())
    {
        mPointAt->setSourceObject(avatar);
    }
}

//-----------------------------------------------------------------------------
// LLAgent()
//-----------------------------------------------------------------------------
LLAgentCamera::~LLAgentCamera()
{
    cleanup();

    // *Note: this is where LLViewerCamera::getInstance() used to be deleted.
}

// Change camera back to third person, stop the autopilot,
// deselect stuff, etc.
//-----------------------------------------------------------------------------
// resetView()
//-----------------------------------------------------------------------------
// <FS:CR> FIRE-8798: Option to prevent camera reset on movement
//void LLAgentCamera::resetView(bool reset_camera, bool change_camera)
void LLAgentCamera::resetView(bool reset_camera, bool change_camera, bool movement)
// </FS:CR>
{
    if (gDisconnected)
    {
        return;
    }

    if (gAgent.getAutoPilot())
    {
        gAgent.stopAutoPilot(true);
    }

    LLSelectMgr::getInstance()->unhighlightAll();

    // By popular request, keep land selection while walking around. JC
    // LLViewerParcelMgr::getInstance()->deselectLand();

    // force deselect when walking and attachment is selected
    // this is so people don't wig out when their avatar moves without animating
    if (LLSelectMgr::getInstance()->getSelection()->isAttachment())
    {
        LLSelectMgr::getInstance()->deselectAll();
    }

    if (gMenuHolder != NULL)
    {
        // Hide all popup menus
        gMenuHolder->hideMenus();
    }

    // <FS:CR> FIRE-8798: Option to prevent camera reset on movement
    static LLCachedControl<bool> sResetCameraOnMovement(gSavedSettings, "FSResetCameraOnMovement");
    if (sResetCameraOnMovement || movement == false)
    {
    // </FS:CR>

    if (change_camera && !gSavedSettings.getBOOL("FreezeTime"))
    {
        changeCameraToDefault();

        if (LLViewerJoystick::getInstance()->getOverrideCamera())
        {
            handle_toggle_flycam();
        }

        // reset avatar mode from eventual residual motion
        if (LLToolMgr::getInstance()->inBuildMode())
        {
            LLViewerJoystick::getInstance()->moveAvatar(true);
        }

        //Camera Tool is needed for Free Camera Control Mode
        if (!LLFloaterCamera::inFreeCameraMode())
        {
            LLFloaterReg::hideInstance("build");

            // Switch back to basic toolset
            LLToolMgr::getInstance()->setCurrentToolset(gBasicToolset);
        }

        gViewerWindow->showCursor();
    }


    if (reset_camera && !gSavedSettings.getBOOL("FreezeTime"))
    {
        if (!gViewerWindow->getLeftMouseDown() && cameraThirdPerson())
        {
            // leaving mouse-steer mode
            LLVector3 agent_at_axis = gAgent.getAtAxis();
            agent_at_axis -= projected_vec(agent_at_axis, gAgent.getReferenceUpVector());
            agent_at_axis.normalize();
            gAgent.resetAxes(lerp(gAgent.getAtAxis(), agent_at_axis, LLSmoothInterpolation::getInterpolant(0.3f)));
        }

        setFocusOnAvatar(true, ANIMATE);

        mCameraFOVZoomFactor = 0.f;
// <FS:Chanayane> Camera roll (from Alchemy)
		resetCameraRoll();
// </FS:Chanayane>
    }
    resetPanDiff();
    resetOrbitDiff();
    mHUDTargetZoom = 1.f;
// <FS:CR> FIRE-8798: Option to prevent camera reset on movement
    }
// </FS:CR>

    if (LLSelectMgr::getInstance()->mAllowSelectAvatar)
    {
        // resetting camera also resets position overrides in debug mode 'AllowSelectAvatar'
        LLObjectSelectionHandle selected_handle = LLSelectMgr::getInstance()->getSelection();
        if (selected_handle->getObjectCount() == 1
            && selected_handle->getFirstObject() != NULL
            && selected_handle->getFirstObject()->isAvatar())
        {
            LLSelectMgr::getInstance()->resetObjectOverrides(selected_handle);
        }
    }
}

// Allow camera to be moved somewhere other than behind avatar.
//-----------------------------------------------------------------------------
// unlockView()
//-----------------------------------------------------------------------------
void LLAgentCamera::unlockView()
{
    if (getFocusOnAvatar())
    {
        if (isAgentAvatarValid())
        {
            setFocusGlobal(LLVector3d::zero, gAgentAvatarp->mID);
        }
        setFocusOnAvatar(false, false); // no animation
    }
}

//-----------------------------------------------------------------------------
// slamLookAt()
//-----------------------------------------------------------------------------
void LLAgentCamera::slamLookAt(const LLVector3 &look_at)
{
    LLVector3 look_at_norm = look_at;
    look_at_norm.mV[VZ] = 0.f;
    look_at_norm.normalize();
    gAgent.resetAxes(look_at_norm);
}

//-----------------------------------------------------------------------------
// calcFocusOffset()
//-----------------------------------------------------------------------------
LLVector3 LLAgentCamera::calcFocusOffset(LLViewerObject *object, LLVector3 original_focus_point, S32 x, S32 y)
{
    LLMatrix4 obj_matrix = object->getRenderMatrix();
    LLQuaternion obj_rot = object->getRenderRotation();
    LLVector3 obj_pos = object->getRenderPosition();

    // if is avatar - don't do any funk heuristics to position the focal point
    // see DEV-30589
    if ((object->isAvatar() && !object->isRoot()) || (object->isAnimatedObject() && object->getControlAvatar()))
    {
        return original_focus_point - obj_pos;
    }
    if (object->isAvatar())
    {
        LLVOAvatar* av = object->asAvatar();
        return original_focus_point - av->getCharacterPosition();
    }

    LLQuaternion inv_obj_rot = ~obj_rot; // get inverse of rotation
    LLVector3 object_extents = object->getScale();

    // make sure they object extents are non-zero
    object_extents.clamp(0.001f, F32_MAX);

    // obj_to_cam_ray is unit vector pointing from object center to camera, in the coordinate frame of the object
    LLVector3 obj_to_cam_ray = obj_pos - LLViewerCamera::getInstance()->getOrigin();
    obj_to_cam_ray.rotVec(inv_obj_rot);
    obj_to_cam_ray.normalize();

    // obj_to_cam_ray_proportions are the (positive) ratios of
    // the obj_to_cam_ray x,y,z components with the x,y,z object dimensions.
    LLVector3 obj_to_cam_ray_proportions;
    obj_to_cam_ray_proportions.mV[VX] = llabs(obj_to_cam_ray.mV[VX] / object_extents.mV[VX]);
    obj_to_cam_ray_proportions.mV[VY] = llabs(obj_to_cam_ray.mV[VY] / object_extents.mV[VY]);
    obj_to_cam_ray_proportions.mV[VZ] = llabs(obj_to_cam_ray.mV[VZ] / object_extents.mV[VZ]);

    // find the largest ratio stored in obj_to_cam_ray_proportions
    // this corresponds to the object's local axial plane (XY, YZ, XZ) that is *most* facing the camera
    LLVector3 longest_object_axis;
    // is x-axis longest?
    if (obj_to_cam_ray_proportions.mV[VX] > obj_to_cam_ray_proportions.mV[VY]
        && obj_to_cam_ray_proportions.mV[VX] > obj_to_cam_ray_proportions.mV[VZ])
    {
        // then grab it
        longest_object_axis.setVec(obj_matrix.getFwdRow4());
    }
    // is y-axis longest?
    else if (obj_to_cam_ray_proportions.mV[VY] > obj_to_cam_ray_proportions.mV[VZ])
    {
        // then grab it
        longest_object_axis.setVec(obj_matrix.getLeftRow4());
    }
    // otherwise, use z axis
    else
    {
        longest_object_axis.setVec(obj_matrix.getUpRow4());
    }

    // Use this axis as the normal to project mouse click on to plane with that normal, at the object center.
    // This generates a point behind the mouse cursor that is approximately in the middle of the object in
    // terms of depth.
    // We do this to allow the camera rotation tool to "tumble" the object by rotating the camera.
    // If the focus point were the object surface under the mouse, camera rotation would introduce an undesirable
    // eccentricity to the object orientation
    LLVector3 focus_plane_normal(longest_object_axis);
    focus_plane_normal.normalize();

    LLVector3d focus_pt_global;
    gViewerWindow->mousePointOnPlaneGlobal(focus_pt_global, x, y, gAgent.getPosGlobalFromAgent(obj_pos), focus_plane_normal);
    LLVector3 focus_pt = gAgent.getPosAgentFromGlobal(focus_pt_global);

    // find vector from camera to focus point in object space
    LLVector3 camera_to_focus_vec = focus_pt - LLViewerCamera::getInstance()->getOrigin();
    camera_to_focus_vec.rotVec(inv_obj_rot);

    // find vector from object origin to focus point in object coordinates
    LLVector3 focus_offset_from_object_center = focus_pt - obj_pos;
    // convert to object-local space
    focus_offset_from_object_center.rotVec(inv_obj_rot);

    // We need to project the focus point back into the bounding box of the focused object.
    // Do this by calculating the XYZ scale factors needed to get focus offset back in bounds along the camera_focus axis
    LLVector3 clip_fraction;

    // for each axis...
    for (U32 axis = VX; axis <= VZ; axis++)
    {
        //...calculate distance that focus offset sits outside of bounding box along that axis...
        //NOTE: dist_out_of_bounds keeps the sign of focus_offset_from_object_center
        F32 dist_out_of_bounds;
        if (focus_offset_from_object_center.mV[axis] > 0.f)
        {
            dist_out_of_bounds = llmax(0.f, focus_offset_from_object_center.mV[axis] - (object_extents.mV[axis] * 0.5f));
        }
        else
        {
            dist_out_of_bounds = llmin(0.f, focus_offset_from_object_center.mV[axis] + (object_extents.mV[axis] * 0.5f));
        }

        //...then calculate the scale factor needed to push camera_to_focus_vec back in bounds along current axis
        if (llabs(camera_to_focus_vec.mV[axis]) < 0.0001f)
        {
            // don't divide by very small number
            clip_fraction.mV[axis] = 0.f;
        }
        else
        {
            clip_fraction.mV[axis] = dist_out_of_bounds / camera_to_focus_vec.mV[axis];
        }
    }

    LLVector3 abs_clip_fraction = clip_fraction;
    abs_clip_fraction.abs();

    // find axis of focus offset that is *most* outside the bounding box and use that to
    // rescale focus offset to inside object extents
    if (abs_clip_fraction.mV[VX] > abs_clip_fraction.mV[VY]
        && abs_clip_fraction.mV[VX] > abs_clip_fraction.mV[VZ])
    {
        focus_offset_from_object_center -= clip_fraction.mV[VX] * camera_to_focus_vec;
    }
    else if (abs_clip_fraction.mV[VY] > abs_clip_fraction.mV[VZ])
    {
        focus_offset_from_object_center -= clip_fraction.mV[VY] * camera_to_focus_vec;
    }
    else
    {
        focus_offset_from_object_center -= clip_fraction.mV[VZ] * camera_to_focus_vec;
    }

    // convert back to world space
    focus_offset_from_object_center.rotVec(obj_rot);

    // now, based on distance of camera from object relative to object size
    // push the focus point towards the near surface of the object when (relatively) close to the objcet
    // or keep the focus point in the object middle when (relatively) far
    // NOTE: leave focus point in middle of avatars, since the behavior you want when alt-zooming on avatars
    // is almost always "tumble about middle" and not "spin around surface point"
    {
        LLVector3 obj_rel = original_focus_point - object->getRenderPosition();

        //now that we have the object relative position, we should bias toward the center of the object
        //based on the distance of the camera to the focus point vs. the distance of the camera to the focus

        F32 relDist = llabs(obj_rel * LLViewerCamera::getInstance()->getAtAxis());
        F32 viewDist = dist_vec(obj_pos + obj_rel, LLViewerCamera::getInstance()->getOrigin());


        LLBBox obj_bbox = object->getBoundingBoxAgent();
        F32 bias = 0.f;

        // virtual_camera_pos is the camera position we are simulating by backing the camera off
        // and adjusting the FOV
        LLVector3 virtual_camera_pos = gAgent.getPosAgentFromGlobal(mFocusTargetGlobal + (getCameraPositionGlobal() - mFocusTargetGlobal) / (1.f + mCameraFOVZoomFactor));

        // if the camera is inside the object (large, hollow objects, for example)
        // leave focus point all the way to destination depth, away from object center
        if(!obj_bbox.containsPointAgent(virtual_camera_pos))
        {
            // perform magic number biasing of focus point towards surface vs. planar center
            bias = clamp_rescale(relDist/viewDist, 0.1f, 0.7f, 0.0f, 1.0f);
            obj_rel = lerp(focus_offset_from_object_center, obj_rel, bias);
        }

        focus_offset_from_object_center = obj_rel;
    }

    return focus_offset_from_object_center;
}

//-----------------------------------------------------------------------------
// calcCameraMinDistance()
//-----------------------------------------------------------------------------
bool LLAgentCamera::calcCameraMinDistance(F32 &obj_min_distance)
{
    bool soft_limit = false; // is the bounding box to be treated literally (volumes) or as an approximation (avatars)

    if (!mFocusObject || mFocusObject->isDead() ||
        mFocusObject->isMesh() ||
        isDisableCameraConstraints())
    {
        obj_min_distance = 0.f;
        return true;
    }

    if (mFocusObject->mDrawable.isNull())
    {
#ifdef LL_RELEASE_FOR_DOWNLOAD
        LL_WARNS() << "Focus object with no drawable!" << LL_ENDL;
#else
        mFocusObject->dump();
        LL_ERRS() << "Focus object with no drawable!" << LL_ENDL;
#endif
        obj_min_distance = 0.f;
        return true;
    }

    LLQuaternion inv_object_rot = ~mFocusObject->getRenderRotation();
    LLVector3 target_offset_origin = mFocusObjectOffset;
    LLVector3 camera_offset_target(getCameraPositionAgent() - gAgent.getPosAgentFromGlobal(mFocusTargetGlobal));

    // convert offsets into object local space
    camera_offset_target.rotVec(inv_object_rot);
    target_offset_origin.rotVec(inv_object_rot);

    // push around object extents based on target offset
    LLVector3 object_extents = mFocusObject->getScale();
    if (mFocusObject->isAvatar())
    {
        // fudge factors that lets you zoom in on avatars a bit more (which don't do FOV zoom)
        object_extents.mV[VX] *= AVATAR_ZOOM_MIN_X_FACTOR;
        object_extents.mV[VY] *= AVATAR_ZOOM_MIN_Y_FACTOR;
        object_extents.mV[VZ] *= AVATAR_ZOOM_MIN_Z_FACTOR;
        soft_limit = true;
    }
    LLVector3 abs_target_offset = target_offset_origin;
    abs_target_offset.abs();

    LLVector3 target_offset_dir = target_offset_origin;

    bool target_outside_object_extents = false;

    for (U32 i = VX; i <= VZ; i++)
    {
        if (abs_target_offset.mV[i] * 2.f > object_extents.mV[i] + OBJECT_EXTENTS_PADDING)
        {
            target_outside_object_extents = true;
        }
        if (camera_offset_target.mV[i] > 0.f)
        {
            object_extents.mV[i] -= target_offset_origin.mV[i] * 2.f;
        }
        else
        {
            object_extents.mV[i] += target_offset_origin.mV[i] * 2.f;
        }
    }

    // don't shrink the object extents so far that the object inverts
    object_extents.clamp(0.001f, F32_MAX);

    // move into first octant
    LLVector3 camera_offset_target_abs_norm = camera_offset_target;
    camera_offset_target_abs_norm.abs();
    // make sure offset is non-zero
    camera_offset_target_abs_norm.clamp(0.001f, F32_MAX);
    camera_offset_target_abs_norm.normalize();

    // find camera position relative to normalized object extents
    LLVector3 camera_offset_target_scaled = camera_offset_target_abs_norm;
    camera_offset_target_scaled.mV[VX] /= object_extents.mV[VX];
    camera_offset_target_scaled.mV[VY] /= object_extents.mV[VY];
    camera_offset_target_scaled.mV[VZ] /= object_extents.mV[VZ];

    if (camera_offset_target_scaled.mV[VX] > camera_offset_target_scaled.mV[VY] &&
        camera_offset_target_scaled.mV[VX] > camera_offset_target_scaled.mV[VZ])
    {
        if (camera_offset_target_abs_norm.mV[VX] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VX] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VX] * 0.5f / camera_offset_target_abs_norm.mV[VX];
        }
    }
    else if (camera_offset_target_scaled.mV[VY] > camera_offset_target_scaled.mV[VZ])
    {
        if (camera_offset_target_abs_norm.mV[VY] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VY] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VY] * 0.5f / camera_offset_target_abs_norm.mV[VY];
        }
    }
    else
    {
        if (camera_offset_target_abs_norm.mV[VZ] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VZ] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VZ] * 0.5f / camera_offset_target_abs_norm.mV[VZ];
        }
    }

    LLVector3 object_split_axis;
    LLVector3 target_offset_scaled = target_offset_origin;
    target_offset_scaled.abs();
    target_offset_scaled.normalize();
    target_offset_scaled.mV[VX] /= object_extents.mV[VX];
    target_offset_scaled.mV[VY] /= object_extents.mV[VY];
    target_offset_scaled.mV[VZ] /= object_extents.mV[VZ];

    if (target_offset_scaled.mV[VX] > target_offset_scaled.mV[VY] &&
        target_offset_scaled.mV[VX] > target_offset_scaled.mV[VZ])
    {
        object_split_axis = LLVector3::x_axis;
    }
    else if (target_offset_scaled.mV[VY] > target_offset_scaled.mV[VZ])
    {
        object_split_axis = LLVector3::y_axis;
    }
    else
    {
        object_split_axis = LLVector3::z_axis;
    }

    LLVector3 camera_offset_object(getCameraPositionAgent() - mFocusObject->getPositionAgent());


    F32 camera_offset_clip = camera_offset_object * object_split_axis;
    F32 target_offset_clip = target_offset_dir * object_split_axis;

    // target has moved outside of object extents
    // check to see if camera and target are on same side
    if (target_outside_object_extents)
    {
        if (camera_offset_clip > 0.f && target_offset_clip > 0.f)
        {
            return false;
        }
        else if (camera_offset_clip < 0.f && target_offset_clip < 0.f)
        {
            return false;
        }
    }

    // clamp obj distance to diagonal of 10 by 10 cube
    obj_min_distance = llmin(obj_min_distance, 10.f * F_SQRT3);

    obj_min_distance += LLViewerCamera::getInstance()->getNear() + (soft_limit ? 0.1f : 0.2f);

    return true;
}

F32 LLAgentCamera::getCameraZoomFraction(bool get_third_person)
{
    // 0.f -> camera zoomed all the way out
    // 1.f -> camera zoomed all the way in
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
    {
        // already [0,1]
        return mHUDTargetZoom;
    }

    if (get_third_person || (mFocusOnAvatar && cameraThirdPerson()))
    {
        return clamp_rescale(mCameraZoomFraction, MIN_ZOOM_FRACTION, MAX_ZOOM_FRACTION, 1.f, 0.f);
    }

    if (cameraCustomizeAvatar())
    {
        F32 distance = (F32)mCameraFocusOffsetTarget.magVec();
        return clamp_rescale(distance, APPEARANCE_MIN_ZOOM, APPEARANCE_MAX_ZOOM, 1.f, 0.f );
    }

    F32 min_zoom;
    F32 max_zoom = getCameraMaxZoomDistance();
    if (isDisableCameraConstraints())
    {
        max_zoom = MAX_CAMERA_DISTANCE_FROM_OBJECT;
    }

    F32 distance = (F32)mCameraFocusOffsetTarget.magVec();
    if (mFocusObject.notNull())
    {
        if (mFocusObject->isAvatar())
        {
            min_zoom = AVATAR_MIN_ZOOM;
        }
        else
        {
            min_zoom = OBJECT_MIN_ZOOM;
        }
    }
    else
    {
        min_zoom = LAND_MIN_ZOOM;
    }

    return clamp_rescale(distance, min_zoom, max_zoom, 1.f, 0.f);
}

void LLAgentCamera::setCameraZoomFraction(F32 fraction)
{
    // 0.f -> camera zoomed all the way out
    // 1.f -> camera zoomed all the way in
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();

    if (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
    {
        mHUDTargetZoom = fraction;
    }
    else if (mFocusOnAvatar && cameraThirdPerson())
    {
        mCameraZoomFraction = rescale(fraction, 0.f, 1.f, MAX_ZOOM_FRACTION, MIN_ZOOM_FRACTION);
    }
    else if (cameraCustomizeAvatar())
    {
        LLVector3d camera_offset_dir = mCameraFocusOffsetTarget;
        camera_offset_dir.normalize();
        mCameraFocusOffsetTarget = camera_offset_dir * rescale(fraction, 0.f, 1.f, APPEARANCE_MAX_ZOOM, APPEARANCE_MIN_ZOOM);
    }
    else
    {
        F32 min_zoom = LAND_MIN_ZOOM;
        F32 max_zoom = getCameraMaxZoomDistance();
        if (isDisableCameraConstraints())
        {
            max_zoom = MAX_CAMERA_DISTANCE_FROM_OBJECT;
        }

        if (mFocusObject.notNull())
        {
            if (mFocusObject->isAvatar())
            {
                min_zoom = AVATAR_MIN_ZOOM;
            }
            else
            {
                min_zoom = OBJECT_MIN_ZOOM;
            }
        }

        LLVector3d camera_offset_dir = mCameraFocusOffsetTarget;
        camera_offset_dir.normalize();
// [RLVa:KB] - Checked: 2.0.0
        const LLVector3d focus_offset_target = camera_offset_dir * rescale(fraction, 0.f, 1.f, max_zoom, min_zoom);
        if ( (RlvActions::isRlvEnabled()) && (!allowFocusOffsetChange(focus_offset_target)) )
            return;
        mCameraFocusOffsetTarget = focus_offset_target;
// [/RLVa:KB]
//      mCameraFocusOffsetTarget = camera_offset_dir * rescale(fraction, 0.f, 1.f, max_zoom, min_zoom);
    }

    startCameraAnimation();
}

F32 LLAgentCamera::getAgentHUDTargetZoom()
{
    static LLCachedControl<F32> hud_scale_factor(gSavedSettings, "HUDScaleFactor");
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    return (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD) ? hud_scale_factor*gAgentCamera.mHUDTargetZoom : hud_scale_factor;
}

//-----------------------------------------------------------------------------
// cameraOrbitAround()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraOrbitAround(const F32 radians)
{
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
    {
        // do nothing for hud selection
    }
    else if (mFocusOnAvatar && (mCameraMode == CAMERA_MODE_THIRD_PERSON || mCameraMode == CAMERA_MODE_FOLLOW))
    {
        gAgent.yaw(radians);
    }
    else
    {
        mOrbitAroundRadians += radians;
        mCameraFocusOffsetTarget.rotVec(radians, 0.f, 0.f, 1.f);

        cameraZoomIn(1.f);
    }
}


//-----------------------------------------------------------------------------
// cameraOrbitOver()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraOrbitOver(const F32 angle)
{
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
    {
        // do nothing for hud selection
    }
    else if (mFocusOnAvatar && mCameraMode == CAMERA_MODE_THIRD_PERSON)
    {
        gAgent.pitch(angle);
    }
    else
    {
        LLVector3 camera_offset_unit(mCameraFocusOffsetTarget);
        camera_offset_unit.normalize();

        F32 angle_from_up = acos( camera_offset_unit * gAgent.getReferenceUpVector() );

        LLVector3d left_axis;
        left_axis.setVec(LLViewerCamera::getInstance()->getLeftAxis());
        F32 new_angle = llclamp(angle_from_up - angle, 1.f * DEG_TO_RAD, 179.f * DEG_TO_RAD);
        mOrbitOverAngle += angle_from_up - new_angle;
        mCameraFocusOffsetTarget.rotVec(angle_from_up - new_angle, left_axis);

        cameraZoomIn(1.f);
    }
}

// <FS:Chanayane> Camera roll (from Alchemy)
//-----------------------------------------------------------------------------
// cameraRollOver()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraRollOver(const F32 angle)
{
    mRollAngle += fmodf(angle, F_TWO_PI);
}

void LLAgentCamera::resetCameraRoll()
{
    mRollAngle = 0.f;
}
// </FS:Chanayane>
void LLAgentCamera::resetCameraOrbit()
{
    LLVector3 camera_offset_unit(mCameraFocusOffsetTarget);
    camera_offset_unit.normalize();

    LLVector3d left_axis;
    left_axis.setVec(LLViewerCamera::getInstance()->getLeftAxis());
    mCameraFocusOffsetTarget.rotVec(-mOrbitOverAngle, left_axis);

    mCameraFocusOffsetTarget.rotVec(-mOrbitAroundRadians, 0.f, 0.f, 1.f);

    cameraZoomIn(1.f);
    resetOrbitDiff();
// <FS:Chanayane> Camera roll (from Alchemy)
	resetCameraRoll();
// </FS:Chanayane>
}

void LLAgentCamera::resetOrbitDiff()
{
    mOrbitAroundRadians = 0;
    mOrbitOverAngle = 0;
}

//-----------------------------------------------------------------------------
// cameraZoomIn()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraZoomIn(const F32 fraction)
{
    if (gDisconnected)
    {
        return;
    }

    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (LLToolMgr::getInstance()->inBuildMode() && selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
    {
        // just update hud zoom level
        mHUDTargetZoom /= fraction;
        return;
    }

    LLVector3d camera_offset_unit(mCameraFocusOffsetTarget);
    F32 current_distance = (F32)camera_offset_unit.normalize();
    F32 new_distance = current_distance * fraction;

    // Unless camera is unlocked
    if (!isDisableCameraConstraints())
    {
        F32 min_zoom = LAND_MIN_ZOOM;

        // Don't move through focus point
        if (mFocusObject)
        {
            LLVector3 camera_offset_dir((F32)camera_offset_unit.mdV[VX], (F32)camera_offset_unit.mdV[VY], (F32)camera_offset_unit.mdV[VZ]);

            if (mFocusObject->isAvatar())
            {
                calcCameraMinDistance(min_zoom);
            }
            else
            {
                min_zoom = OBJECT_MIN_ZOOM;
            }
        }

        new_distance = llmax(new_distance, min_zoom);

        // <FS:Ansariel> FIRE-23470: Fix camera controls zoom glitch
        //F32 max_distance = getCameraMaxZoomDistance();
        F32 max_distance = getCameraMaxZoomDistance(true);
        max_distance = llmin(max_distance, current_distance * 4.f); //Scaled max relative to current distance.  MAINT-3154
        new_distance = llmin(new_distance, max_distance);

        if (cameraCustomizeAvatar())
        {
            new_distance = llclamp(new_distance, APPEARANCE_MIN_ZOOM, APPEARANCE_MAX_ZOOM);
        }
    }

// [RLVa:KB] - Checked: 2.0.0
    if ( (RlvActions::isRlvEnabled()) && (!allowFocusOffsetChange(new_distance * camera_offset_unit)) )
        return;
// [/RLVa:KB]

    mCameraFocusOffsetTarget = new_distance * camera_offset_unit;
}

//-----------------------------------------------------------------------------
// cameraOrbitIn()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraOrbitIn(const F32 meters)
{
    if (mFocusOnAvatar && mCameraMode == CAMERA_MODE_THIRD_PERSON)
    {
// [RLVa:KB] - @setcam_eyeoffsetscale
        F32 camera_offset_dist = llmax(0.001f, getCameraOffsetInitial().magVec() * getCameraOffsetScale());
// [/RLVa:KB]
//      F32 camera_offset_dist = llmax(0.001f, getCameraOffsetInitial().magVec() * gSavedSettings.getF32("CameraOffsetScale"));

        mCameraZoomFraction = (mTargetCameraDistance - meters) / camera_offset_dist;

        if (!gSavedSettings.getBOOL("FreezeTime") && mCameraZoomFraction < MIN_ZOOM_FRACTION && meters > 0.f)
        {
            // No need to animate, camera is already there.
            changeCameraToMouselook(false);
        }

        if (!isDisableCameraConstraints())
        {
            mCameraZoomFraction = llclamp(mCameraZoomFraction, MIN_ZOOM_FRACTION, MAX_ZOOM_FRACTION);
        }
    }
    else
    {
        LLVector3d  camera_offset_unit(mCameraFocusOffsetTarget);
        F32 current_distance = (F32)camera_offset_unit.normalize();
        F32 new_distance = current_distance - meters;

        // Unless camera is unlocked
        if (!isDisableCameraConstraints())
        {
            F32 min_zoom = LAND_MIN_ZOOM;

            // Don't move through focus point
            if (mFocusObject.notNull())
            {
                if (mFocusObject->isAvatar())
                {
                    min_zoom = AVATAR_MIN_ZOOM;
                }
                else
                {
                    min_zoom = OBJECT_MIN_ZOOM;
                }
            }

            new_distance = llmax(new_distance, min_zoom);

            // <FS:Ansariel> FIRE-23470: Fix camera controls zoom glitch
            //F32 max_distance = getCameraMaxZoomDistance();
            F32 max_distance = getCameraMaxZoomDistance(true);
            new_distance = llmin(new_distance, max_distance);

            if (CAMERA_MODE_CUSTOMIZE_AVATAR == getCameraMode())
            {
                new_distance = llclamp(new_distance, APPEARANCE_MIN_ZOOM, APPEARANCE_MAX_ZOOM);
            }
        }

// [RLVa:KB] - Checked: 2.0.0
        if ( (RlvActions::isRlvEnabled()) && (!allowFocusOffsetChange(new_distance * camera_offset_unit)) )
            return;
// [/RLVa:KB]

        // Compute new camera offset
        mCameraFocusOffsetTarget = new_distance * camera_offset_unit;
        cameraZoomIn(1.f);
    }
}

//-----------------------------------------------------------------------------
// cameraPanIn()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraPanIn(F32 meters)
{
    LLVector3d at_axis;
    at_axis.setVec(LLViewerCamera::getInstance()->getAtAxis());

    mPanFocusDiff += meters * at_axis;

    mFocusTargetGlobal += meters * at_axis;
    mFocusGlobal = mFocusTargetGlobal;
    // don't enforce zoom constraints as this is the only way for users to get past them easily
    updateFocusOffset();
    // NOTE: panning movements expect the camera to move exactly with the focus target, not animated behind -Nyx
    mCameraSmoothingLastPositionGlobal = calcCameraPositionTargetGlobal();
}

//-----------------------------------------------------------------------------
// cameraPanLeft()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraPanLeft(F32 meters)
{
    LLVector3d left_axis;
    left_axis.setVec(LLViewerCamera::getInstance()->getLeftAxis());

    mPanFocusDiff += meters * left_axis;

    mFocusTargetGlobal += meters * left_axis;
    mFocusGlobal = mFocusTargetGlobal;

    // disable smoothing for camera pan, which causes some residents unhappiness
    mCameraSmoothingStop = true;

    cameraZoomIn(1.f);
    updateFocusOffset();
    // NOTE: panning movements expect the camera to move exactly with the focus target, not animated behind - Nyx
    mCameraSmoothingLastPositionGlobal = calcCameraPositionTargetGlobal();
}

//-----------------------------------------------------------------------------
// cameraPanUp()
//-----------------------------------------------------------------------------
void LLAgentCamera::cameraPanUp(F32 meters)
{
    LLVector3d up_axis;
    up_axis.setVec(LLViewerCamera::getInstance()->getUpAxis());

    mPanFocusDiff += meters * up_axis;

    mFocusTargetGlobal += meters * up_axis;
    mFocusGlobal = mFocusTargetGlobal;

    // disable smoothing for camera pan, which causes some residents unhappiness
    mCameraSmoothingStop = true;

    cameraZoomIn(1.f);
    updateFocusOffset();
    // NOTE: panning movements expect the camera to move exactly with the focus target, not animated behind -Nyx
    mCameraSmoothingLastPositionGlobal = calcCameraPositionTargetGlobal();
}

void LLAgentCamera::resetCameraPan()
{
    mFocusTargetGlobal -= mPanFocusDiff;

    mFocusGlobal = mFocusTargetGlobal;
    mCameraSmoothingStop = true;

    cameraZoomIn(1.f);
    updateFocusOffset();

    mCameraSmoothingLastPositionGlobal = calcCameraPositionTargetGlobal();

    resetPanDiff();
}

void LLAgentCamera::resetPanDiff()
{
    mPanFocusDiff.clear();
}

//-----------------------------------------------------------------------------
// updateLookAt()
//-----------------------------------------------------------------------------
void LLAgentCamera::updateLookAt(const S32 mouse_x, const S32 mouse_y)
{
    static LLVector3 last_at_axis;

    if (!isAgentAvatarValid()) return;

    LLQuaternion av_inv_rot = ~gAgentAvatarp->mRoot->getWorldRotation();
    LLVector3 root_at = LLVector3::x_axis * gAgentAvatarp->mRoot->getWorldRotation();

    if  (LLTrace::get_frame_recording().getLastRecording().getLastValue(*gViewerWindow->getMouseVelocityStat()) < 0.01f
        && (root_at * last_at_axis > 0.95f))
    {
        LLVector3 vel = gAgentAvatarp->getVelocity();
        if (vel.magVecSquared() > 4.f)
        {
            setLookAt(LOOKAT_TARGET_IDLE, gAgentAvatarp, vel * av_inv_rot);
        }
        else
        {
            // *FIX: rotate mframeagent by sit object's rotation?
            LLQuaternion look_rotation = gAgentAvatarp->isSitting() ? gAgentAvatarp->getRenderRotation() : gAgent.getFrameAgent().getQuaternion(); // use camera's current rotation
            LLVector3 look_offset = LLVector3(2.f, 0.f, 0.f) * look_rotation * av_inv_rot;
            setLookAt(LOOKAT_TARGET_IDLE, gAgentAvatarp, look_offset);
        }
        last_at_axis = root_at;
        return;
    }

    last_at_axis = root_at;

    if (CAMERA_MODE_CUSTOMIZE_AVATAR == getCameraMode())
    {
        setLookAt(LOOKAT_TARGET_NONE, gAgentAvatarp, LLVector3(-2.f, 0.f, 0.f));
    }
    else
    {
        // Move head based on cursor position
        ELookAtType lookAtType = LOOKAT_TARGET_NONE;
        LLVector3 headLookAxis;
        LLCoordFrame frameCamera = *((LLCoordFrame*)LLViewerCamera::getInstance());

        if (cameraMouselook())
        {
            lookAtType = LOOKAT_TARGET_MOUSELOOK;
        }
        else if (cameraThirdPerson())
        {
            // range from -.5 to .5
            F32 x_from_center =
                ((F32) mouse_x / (F32) gViewerWindow->getWorldViewWidthScaled() ) - 0.5f;
            F32 y_from_center =
                ((F32) mouse_y / (F32) gViewerWindow->getWorldViewHeightScaled() ) - 0.5f;

            frameCamera.yaw( - x_from_center * gSavedSettings.getF32("YawFromMousePosition") * DEG_TO_RAD);
            frameCamera.pitch( - y_from_center * gSavedSettings.getF32("PitchFromMousePosition") * DEG_TO_RAD);
            lookAtType = LOOKAT_TARGET_FREELOOK;
        }

        headLookAxis = frameCamera.getAtAxis();
        // RN: we use world-space offset for mouselook and freelook
        //headLookAxis = headLookAxis * av_inv_rot;
        setLookAt(lookAtType, gAgentAvatarp, headLookAxis);
    }
}

static LLTrace::BlockTimerStatHandle FTM_UPDATE_CAMERA("Camera");

extern bool gCubeSnapshot;

//-----------------------------------------------------------------------------
// updateCamera()
//-----------------------------------------------------------------------------
void LLAgentCamera::updateCamera()
{
    LL_RECORD_BLOCK_TIME(FTM_UPDATE_CAMERA);
    if (gCubeSnapshot)
    {
        return;
    }

    // - changed camera_skyward to the new global "mCameraUpVector"
    mCameraUpVector = LLVector3::z_axis;
    //LLVector3 camera_skyward(0.f, 0.f, 1.f);

// [RLVa:KB] - Checked: RLVa-2.0.0
    // Set focus back on our avie if something changed it
    if ( (gRlvHandler.hasBehaviour(RLV_BHVR_SETCAM_UNLOCK)) && ((cameraThirdPerson()) || (cameraFollow())) && (!getFocusOnAvatar()) )
    {
        setFocusOnAvatar(true, false);
    }
// [/RLVa:KB]

    U32 camera_mode = mCameraAnimating ? mLastCameraMode : mCameraMode;

    validateFocusObject();

    if (isAgentAvatarValid() &&
        gAgentAvatarp->isSitting() &&
        camera_mode == CAMERA_MODE_MOUSELOOK)
    {
        //changed camera_skyward to the new global "mCameraUpVector"
        mCameraUpVector = mCameraUpVector * gAgentAvatarp->getRenderRotation();
    }

    if (cameraThirdPerson() && (mFocusOnAvatar || mAllowChangeToFollow) && LLFollowCamMgr::getInstance()->getActiveFollowCamParams())
    {
        mAllowChangeToFollow = false;
        mFocusOnAvatar = true;
        changeCameraToFollow();
    }

    //NOTE - this needs to be integrated into a general upVector system here within llAgent.
    if ( camera_mode == CAMERA_MODE_FOLLOW && mFocusOnAvatar )
    {
        mCameraUpVector = mFollowCam.getUpVector();
    }

    if (mSitCameraEnabled)
    {
        if (mSitCameraReferenceObject->isDead())
        {
            setSitCamera(LLUUID::null);
        }
    }

    // Update UI with our camera inputs
    LLFloaterCamera* camera_floater = LLFloaterReg::findTypedInstance<LLFloaterCamera>("camera");
    if (camera_floater)
    {
        camera_floater->mRotate->setToggleState(gAgentCamera.getOrbitRightKey() > 0.f,  // left
                                                gAgentCamera.getOrbitUpKey() > 0.f,     // top
                                                gAgentCamera.getOrbitLeftKey() > 0.f,   // right
                                                gAgentCamera.getOrbitDownKey() > 0.f);  // bottom

        camera_floater->mTrack->setToggleState(gAgentCamera.getPanLeftKey() > 0.f,      // left
                                               gAgentCamera.getPanUpKey() > 0.f,            // top
                                               gAgentCamera.getPanRightKey() > 0.f,     // right
                                               gAgentCamera.getPanDownKey() > 0.f);     // bottom
    }

    // <FS:Ansariel> Phototools camera
    camera_floater = LLFloaterReg::findTypedInstance<LLFloaterCamera>("phototools_camera");
    if (camera_floater)
    {
        camera_floater->mRotate->setToggleState(gAgentCamera.getOrbitRightKey() > 0.f,  // left
                                                gAgentCamera.getOrbitUpKey() > 0.f,     // top
                                                gAgentCamera.getOrbitLeftKey() > 0.f,   // right
                                                gAgentCamera.getOrbitDownKey() > 0.f);  // bottom

        camera_floater->mTrack->setToggleState(gAgentCamera.getPanLeftKey() > 0.f,      // left
                                               gAgentCamera.getPanUpKey() > 0.f,            // top
                                               gAgentCamera.getPanRightKey() > 0.f,     // right
                                               gAgentCamera.getPanDownKey() > 0.f);     // bottom
    }
    // </FS:Ansariel>

    // <FS:Ansariel> Optional small camera floater
    camera_floater = LLFloaterReg::findTypedInstance<LLFloaterCamera>("fs_camera_small");
    if (camera_floater)
    {
        camera_floater->mRotate->setToggleState(gAgentCamera.getOrbitRightKey() > 0.f,  // left
                                                gAgentCamera.getOrbitUpKey() > 0.f,     // top
                                                gAgentCamera.getOrbitLeftKey() > 0.f,   // right
                                                gAgentCamera.getOrbitDownKey() > 0.f);  // bottom

        camera_floater->mTrack->setToggleState(gAgentCamera.getPanLeftKey() > 0.f,      // left
                                               gAgentCamera.getPanUpKey() > 0.f,            // top
                                               gAgentCamera.getPanRightKey() > 0.f,     // right
                                               gAgentCamera.getPanDownKey() > 0.f);     // bottom
    }
    // </FS:Ansariel>

    // Handle camera movement based on keyboard.
    const F32 ORBIT_OVER_RATE = 90.f * DEG_TO_RAD;          // radians per second
    const F32 ORBIT_AROUND_RATE = 90.f * DEG_TO_RAD;        // radians per second
    const F32 PAN_RATE = 5.f;                               // meters per second
// <FS:Chanayane> Camera roll (from Alchemy)
    const F32 ROLL_RATE = 45.f * DEG_TO_RAD;                // radians per second
// </FS:Chanayane>

    if (gAgentCamera.getOrbitUpKey() || gAgentCamera.getOrbitDownKey())
    {
        F32 input_rate = gAgentCamera.getOrbitUpKey() - gAgentCamera.getOrbitDownKey();
        cameraOrbitOver( input_rate * ORBIT_OVER_RATE / gFPSClamped );
    }

    if (gAgentCamera.getOrbitLeftKey() || gAgentCamera.getOrbitRightKey())
    {
        F32 input_rate = gAgentCamera.getOrbitLeftKey() - gAgentCamera.getOrbitRightKey();
        cameraOrbitAround(input_rate * ORBIT_AROUND_RATE / gFPSClamped);
    }

    if (gAgentCamera.getOrbitInKey() || gAgentCamera.getOrbitOutKey())
    {
        F32 input_rate = gAgentCamera.getOrbitInKey() - gAgentCamera.getOrbitOutKey();

        LLVector3d to_focus = gAgent.getPosGlobalFromAgent(LLViewerCamera::getInstance()->getOrigin()) - calcFocusPositionTargetGlobal();
        F32 distance_to_focus = (F32)to_focus.magVec();
        // Move at distance (in meters) meters per second
        cameraOrbitIn( input_rate * distance_to_focus / gFPSClamped );
    }

    if (gAgentCamera.getPanInKey() || gAgentCamera.getPanOutKey())
    {
        F32 input_rate = gAgentCamera.getPanInKey() - gAgentCamera.getPanOutKey();
        cameraPanIn(input_rate * PAN_RATE / gFPSClamped);
    }

    if (gAgentCamera.getPanRightKey() || gAgentCamera.getPanLeftKey())
    {
        F32 input_rate = gAgentCamera.getPanRightKey() - gAgentCamera.getPanLeftKey();
        cameraPanLeft(input_rate * -PAN_RATE / gFPSClamped );
    }

    if (gAgentCamera.getPanUpKey() || gAgentCamera.getPanDownKey())
    {
        F32 input_rate = gAgentCamera.getPanUpKey() - gAgentCamera.getPanDownKey();
        cameraPanUp(input_rate * PAN_RATE / gFPSClamped );
    }

// <FS:Chanayane> Camera roll (from Alchemy)
    if (getRollLeftKey() || getRollRightKey())
    {
        F32 input_rate = getRollRightKey() - getRollLeftKey();
        cameraRollOver(input_rate * ROLL_RATE / gFPSClamped);
    }
// </FS:Chanayane>

    // Clear camera keyboard keys.
    gAgentCamera.clearOrbitKeys();
    gAgentCamera.clearPanKeys();

    // lerp camera focus offset
    mCameraFocusOffset = lerp(mCameraFocusOffset, mCameraFocusOffsetTarget, LLSmoothInterpolation::getInterpolant(CAMERA_FOCUS_HALF_LIFE));

    if ( mCameraMode == CAMERA_MODE_FOLLOW )
    {
        if (isAgentAvatarValid())
        {
            //--------------------------------------------------------------------------------
            // this is where the avatar's position and rotation are given to followCam, and
            // where it is updated. All three of its attributes are updated: (1) position,
            // (2) focus, and (3) upvector. They can then be queried elsewhere in llAgent.
            //--------------------------------------------------------------------------------
            // *TODO: use combined rotation of frameagent and sit object
            LLQuaternion avatarRotationForFollowCam = gAgentAvatarp->isSitting() ? gAgentAvatarp->getRenderRotation() : gAgent.getFrameAgent().getQuaternion();

            LLFollowCamParams* current_cam = LLFollowCamMgr::getInstance()->getActiveFollowCamParams();
            if (current_cam)
            {
                mFollowCam.copyParams(*current_cam);
                mFollowCam.setSubjectPositionAndRotation( gAgentAvatarp->getRenderPosition(), avatarRotationForFollowCam );
                mFollowCam.update();
                LLViewerJoystick::getInstance()->setCameraNeedsUpdate(true);
            }
            else
            {
                changeCameraToThirdPerson(true);
            }
        }
    }

    bool hit_limit;
    LLVector3d camera_pos_global;
    LLVector3d camera_target_global = calcCameraPositionTargetGlobal(&hit_limit);
    mCameraVirtualPositionAgent = gAgent.getPosAgentFromGlobal(camera_target_global);
    LLVector3d focus_target_global = calcFocusPositionTargetGlobal();

    // perform field of view correction
    mCameraFOVZoomFactor = calcCameraFOVZoomFactor();
    camera_target_global = focus_target_global + (camera_target_global - focus_target_global) * (1.f + mCameraFOVZoomFactor);

    gAgent.setShowAvatar(true); // can see avatar by default

    // Adjust position for animation
    if (mCameraAnimating)
    {
        F32 time = mAnimationTimer.getElapsedTimeF32();

        // yet another instance of critically damped motion, hooray!
        // F32 fraction_of_animation = 1.f - pow(2.f, -time / CAMERA_ZOOM_HALF_LIFE);

        // linear interpolation
        F32 fraction_of_animation = time / mAnimationDuration;

        bool isfirstPerson = mCameraMode == CAMERA_MODE_MOUSELOOK;
        bool wasfirstPerson = mLastCameraMode == CAMERA_MODE_MOUSELOOK;
        F32 fraction_animation_to_skip;

        if (mAnimationCameraStartGlobal == camera_target_global)
        {
            fraction_animation_to_skip = 0.f;
        }
        else
        {
            LLVector3d cam_delta = mAnimationCameraStartGlobal - camera_target_global;
            fraction_animation_to_skip = HEAD_BUFFER_SIZE / (F32)cam_delta.magVec();
        }
        F32 animation_start_fraction = (wasfirstPerson) ? fraction_animation_to_skip : 0.f;
        F32 animation_finish_fraction =  (isfirstPerson) ? (1.f - fraction_animation_to_skip) : 1.f;

        if (fraction_of_animation < animation_finish_fraction)
        {
            if (fraction_of_animation < animation_start_fraction || fraction_of_animation > animation_finish_fraction )
            {
                gAgent.setShowAvatar(false);
            }

            // ...adjust position for animation
            F32 smooth_fraction_of_animation = llsmoothstep(0.0f, 1.0f, fraction_of_animation);
            camera_pos_global = lerp(mAnimationCameraStartGlobal, camera_target_global, smooth_fraction_of_animation);
            mFocusGlobal = lerp(mAnimationFocusStartGlobal, focus_target_global, smooth_fraction_of_animation);
        }
        else
        {
            // ...animation complete
            mCameraAnimating = false;

            camera_pos_global = camera_target_global;
            mFocusGlobal = focus_target_global;

            gAgent.endAnimationUpdateUI();
            gAgent.setShowAvatar(true);
        }

        if (isAgentAvatarValid() && (mCameraMode != CAMERA_MODE_MOUSELOOK))
        {
            gAgentAvatarp->updateAttachmentVisibility(mCameraMode);
        }
    }
    else
    {
        camera_pos_global = camera_target_global;
        mFocusGlobal = focus_target_global;
        gAgent.setShowAvatar(true);
    }

    // smoothing
    if (true)
    {
        LLVector3d agent_pos = gAgent.getPositionGlobal();
        LLVector3d camera_pos_agent = camera_pos_global - agent_pos;
        // Sitting on what you're manipulating can cause camera jitter with smoothing.
        // This turns off smoothing while editing. -MG
        bool in_build_mode = LLToolMgr::getInstance()->inBuildMode();
        mCameraSmoothingStop = mCameraSmoothingStop || in_build_mode;

        if (cameraThirdPerson() && !mCameraSmoothingStop)
        {
            const F32 SMOOTHING_HALF_LIFE = 0.02f;

            F32 smoothing = LLSmoothInterpolation::getInterpolant(gSavedSettings.getF32("CameraPositionSmoothing") * SMOOTHING_HALF_LIFE, false);

            if (mFocusOnAvatar && !mFocusObject) // we differentiate on avatar mode
            {
                // for avatar-relative focus, we smooth in avatar space -
                // the avatar moves too jerkily w/r/t global space to smooth there.

                LLVector3d delta = camera_pos_agent - mCameraSmoothingLastPositionAgent;
                if (delta.magVec() < MAX_CAMERA_SMOOTH_DISTANCE)  // only smooth over short distances please
                {
                    camera_pos_agent = lerp(mCameraSmoothingLastPositionAgent, camera_pos_agent, smoothing);
                    camera_pos_global = camera_pos_agent + agent_pos;
                }
            }
            else
            {
                LLVector3d delta = camera_pos_global - mCameraSmoothingLastPositionGlobal;
                if (delta.magVec() < MAX_CAMERA_SMOOTH_DISTANCE) // only smooth over short distances please
                {
                    camera_pos_global = lerp(mCameraSmoothingLastPositionGlobal, camera_pos_global, smoothing);
                }
            }
        }

        mCameraSmoothingLastPositionGlobal = camera_pos_global;
        mCameraSmoothingLastPositionAgent = camera_pos_agent;
        mCameraSmoothingStop = false;
    }


    mCameraCurrentFOVZoomFactor = lerp(mCameraCurrentFOVZoomFactor, mCameraFOVZoomFactor, LLSmoothInterpolation::getInterpolant(FOV_ZOOM_HALF_LIFE));

//  LL_INFOS() << "Current FOV Zoom: " << mCameraCurrentFOVZoomFactor << " Target FOV Zoom: " << mCameraFOVZoomFactor << " Object penetration: " << mFocusObjectDist << LL_ENDL;

    LLVector3 focus_agent = gAgent.getPosAgentFromGlobal(mFocusGlobal);

    mCameraPositionAgent = gAgent.getPosAgentFromGlobal(camera_pos_global);

    // Move the camera

    LLViewerCamera::getInstance()->updateCameraLocation(mCameraPositionAgent, mCameraUpVector, focus_agent);
    //LLViewerCamera::getInstance()->updateCameraLocation(mCameraPositionAgent, camera_skyward, focus_agent);

    // Change FOV
    LLViewerCamera::getInstance()->setView(LLViewerCamera::getInstance()->getDefaultFOV() / (1.f + mCameraCurrentFOVZoomFactor));

    // follow camera when in customize mode
    if (cameraCustomizeAvatar())
    {
        setLookAt(LOOKAT_TARGET_FOCUS, NULL, mCameraPositionAgent);
    }

    // update the travel distance stat
    // this isn't directly related to the camera
    // but this seemed like the best place to do this
    LLVector3d global_pos = gAgent.getPositionGlobal();
    if (!gAgent.getLastPositionGlobal().isExactlyZero())
    {
        LLVector3d delta = global_pos - gAgent.getLastPositionGlobal();
        gAgent.setDistanceTraveled(gAgent.getDistanceTraveled() + delta.magVec());
    }
    gAgent.setLastPositionGlobal(global_pos);

    if (LLVOAvatar::sVisibleInFirstPerson && isAgentAvatarValid() && !gAgentAvatarp->isSitting() && cameraMouselook())
    {
        LLVector3 head_pos = gAgentAvatarp->mHeadp->getWorldPosition() +
            LLVector3(0.08f, 0.f, 0.05f) * gAgentAvatarp->mHeadp->getWorldRotation() +
            LLVector3(0.1f, 0.f, 0.f) * gAgentAvatarp->mPelvisp->getWorldRotation();
        LLVector3 diff = mCameraPositionAgent - head_pos;
        diff = diff * ~gAgentAvatarp->mRoot->getWorldRotation();

        LLJoint* torso_joint = gAgentAvatarp->mTorsop;
        LLJoint* chest_joint = gAgentAvatarp->mChestp;
        LLVector3 torso_scale = torso_joint->getScale();
        LLVector3 chest_scale = chest_joint->getScale();

        // shorten avatar skeleton to avoid foot interpenetration
        // <FS:Ansariel> FIRE-10574: Attachments in mouselook glitching up
        //if (!gAgentAvatarp->mInAir)
        //{
        //  LLVector3 chest_offset = LLVector3(0.f, 0.f, chest_joint->getPosition().mV[VZ]) * torso_joint->getWorldRotation();
        //  F32 z_compensate = llclamp(-diff.mV[VZ], -0.2f, 1.f);
        //  F32 scale_factor = llclamp(1.f - ((z_compensate * 0.5f) / chest_offset.mV[VZ]), 0.5f, 1.2f);
        //  torso_joint->setScale(LLVector3(1.f, 1.f, scale_factor));

        //  LLJoint* neck_joint = gAgentAvatarp->mNeckp;
        //  LLVector3 neck_offset = LLVector3(0.f, 0.f, neck_joint->getPosition().mV[VZ]) * chest_joint->getWorldRotation();
        //  scale_factor = llclamp(1.f - ((z_compensate * 0.5f) / neck_offset.mV[VZ]), 0.5f, 1.2f);
        //  chest_joint->setScale(LLVector3(1.f, 1.f, scale_factor));
        //  diff.mV[VZ] = 0.f;
        //}
        // </FS:Ansariel>

        // SL-315
        gAgentAvatarp->mPelvisp->setPosition(gAgentAvatarp->mPelvisp->getPosition() + diff);

        gAgentAvatarp->mRoot->updateWorldMatrixChildren();

        for (LLVOAvatar::attachment_map_t::iterator iter = gAgentAvatarp->mAttachmentPoints.begin();
             iter != gAgentAvatarp->mAttachmentPoints.end(); )
        {
            LLVOAvatar::attachment_map_t::iterator curiter = iter++;
            LLViewerJointAttachment* attachment = curiter->second;
            for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                 attachment_iter != attachment->mAttachedObjects.end();
                 ++attachment_iter)
            {
                LLViewerObject *attached_object = attachment_iter->get();
                if (attached_object && !attached_object->isDead() && attached_object->mDrawable.notNull())
                {
                    // clear any existing "early" movements of attachment
                    attached_object->mDrawable->clearState(LLDrawable::EARLY_MOVE);
                    gPipeline.updateMoveNormalAsync(attached_object->mDrawable);
                    attached_object->updateText();
                }
            }
        }

        torso_joint->setScale(torso_scale);
        chest_joint->setScale(chest_scale);
    }
// <FS:Chanayane> Camera roll (from Alchemy)
    //     We have do this at the very end to make sure it takes all previous calculations into
    //     account and then applies our roll on top of it, besides it wouldn't even work otherwise.
    LLQuaternion rot_quat = LLViewerCamera::getInstance()->getQuaternion();
    LLMatrix3 rot_mat(mRollAngle, 0.f, 0.f);
    rot_quat = LLQuaternion(rot_mat)*rot_quat;

    LLMatrix3 mat(rot_quat);

    LLViewerCamera::getInstance()->mXAxis = LLVector3(mat.mMatrix[0]);
    LLViewerCamera::getInstance()->mYAxis = LLVector3(mat.mMatrix[1]);
    LLViewerCamera::getInstance()->mZAxis = LLVector3(mat.mMatrix[2]);
}
// </FS:Chanayane>

void LLAgentCamera::updateLastCamera()
{
    mLastCameraMode = mCameraMode;
}

void LLAgentCamera::updateFocusOffset()
{
    validateFocusObject();
    if (mFocusObject.notNull())
    {
        LLVector3d obj_pos = gAgent.getPosGlobalFromAgent(mFocusObject->getRenderPosition());
        mFocusObjectOffset.setVec(mFocusTargetGlobal - obj_pos);
    }
}

void LLAgentCamera::validateFocusObject()
{
    if (mFocusObject.notNull() &&
        mFocusObject->isDead())
    {
        mFocusObjectOffset.clearVec();
        clearFocusObject();
        mCameraFOVZoomFactor = 0.f;
    }
}

//-----------------------------------------------------------------------------
// calcFocusPositionTargetGlobal()
//-----------------------------------------------------------------------------
LLVector3d LLAgentCamera::calcFocusPositionTargetGlobal()
{
    if (mFocusObject.notNull() && mFocusObject->isDead())
    {
        clearFocusObject();
    }

    if (mCameraMode == CAMERA_MODE_FOLLOW && mFocusOnAvatar)
    {
        mFocusTargetGlobal = gAgent.getPosGlobalFromAgent(mFollowCam.getSimulatedFocus());
        return mFocusTargetGlobal;
    }
    else if (mCameraMode == CAMERA_MODE_MOUSELOOK)
    {
        LLVector3d at_axis(1.0, 0.0, 0.0);
        LLQuaternion agent_rot = gAgent.getFrameAgent().getQuaternion();
        if (isAgentAvatarValid() && gAgentAvatarp->getParent())
        {
            LLViewerObject* root_object = (LLViewerObject*)gAgentAvatarp->getRoot();
            if (!root_object->flagCameraDecoupled())
            {
                agent_rot *= ((LLViewerObject*)(gAgentAvatarp->getParent()))->getRenderRotation();
            }
        }
        at_axis = at_axis * agent_rot;
        mFocusTargetGlobal = calcCameraPositionTargetGlobal() + at_axis;
        return mFocusTargetGlobal;
    }
    else if (mCameraMode == CAMERA_MODE_CUSTOMIZE_AVATAR)
    {
        if (mFocusOnAvatar)
        {
            LLVector3 focus_target = isAgentAvatarValid()
                ? gAgentAvatarp->mHeadp->getWorldPosition()
                : gAgent.getPositionAgent();
            LLVector3d focus_target_global = gAgent.getPosGlobalFromAgent(focus_target);
            mFocusTargetGlobal = focus_target_global;
        }
        return mFocusTargetGlobal;
    }
    else if (!mFocusOnAvatar)
    {
        if (mFocusObject.notNull() && !mFocusObject->isDead() && mFocusObject->mDrawable.notNull())
        {
            LLDrawable* drawablep = mFocusObject->mDrawable;

            if (mTrackFocusObject &&
                drawablep &&
                drawablep->isActive())
            {
                if (!mFocusObject->isAvatar())
                {
                    if (mFocusObject->isSelected())
                    {
                        gPipeline.updateMoveNormalAsync(drawablep);
                    }
                    else
                    {
                        if (drawablep->isState(LLDrawable::MOVE_UNDAMPED))
                        {
                            gPipeline.updateMoveNormalAsync(drawablep);
                        }
                        else
                        {
                            gPipeline.updateMoveDampedAsync(drawablep);
                        }
                    }
                }
            }
            // if not tracking object, update offset based on new object position
            else
            {
                updateFocusOffset();
            }
            LLVector3 focus_agent = mFocusObject->getRenderPosition() + mFocusObjectOffset;
            mFocusTargetGlobal.setVec(gAgent.getPosGlobalFromAgent(focus_agent));
        }
        return mFocusTargetGlobal;
    }
    else if (mSitCameraEnabled && isAgentAvatarValid() && gAgentAvatarp->isSitting() && mSitCameraReferenceObject.notNull())
    {
        // sit camera
        LLVector3 object_pos = mSitCameraReferenceObject->getRenderPosition();
        LLQuaternion object_rot = mSitCameraReferenceObject->getRenderRotation();

        LLVector3 target_pos = object_pos + (mSitCameraFocus * object_rot);
        return gAgent.getPosGlobalFromAgent(target_pos);
    }
    else
    {
        return gAgent.getPositionGlobal() + calcThirdPersonFocusOffset();
    }
}

LLVector3d LLAgentCamera::calcThirdPersonFocusOffset()
{
    // ...offset from avatar
    LLVector3d focus_offset;
    LLQuaternion agent_rot = gAgent.getFrameAgent().getQuaternion();
    if (isAgentAvatarValid() && gAgentAvatarp->getParent())
    {
        agent_rot *= ((LLViewerObject*)(gAgentAvatarp->getParent()))->getRenderRotation();
    }

//    static LLCachedControl<LLVector3d> focus_offset_initial(gSavedSettings, "FocusOffsetRearView", LLVector3d());
// [RLVa:KB] - @setcam_focusoffset
    LLVector3d focus_offset_initial = getFocusOffsetInitial();
// [/RLVa:KB]
    return focus_offset_initial * agent_rot;
}

void LLAgentCamera::setupSitCamera()
{
    // agent frame entering this function is in world coordinates
    if (isAgentAvatarValid() && gAgentAvatarp->getParent())
    {
        LLQuaternion parent_rot = ((LLViewerObject*)gAgentAvatarp->getParent())->getRenderRotation();
        // slam agent coordinate frame to proper parent local version
        LLVector3 at_axis = gAgent.getFrameAgent().getAtAxis();
        at_axis.mV[VZ] = 0.f;
        at_axis.normalize();
        gAgent.resetAxes(at_axis * ~parent_rot);
    }
}

//-----------------------------------------------------------------------------
// getCameraPositionAgent()
//-----------------------------------------------------------------------------
const LLVector3 &LLAgentCamera::getCameraPositionAgent() const
{
    return LLViewerCamera::getInstance()->getOrigin();
}

//-----------------------------------------------------------------------------
// getCameraPositionGlobal()
//-----------------------------------------------------------------------------
LLVector3d LLAgentCamera::getCameraPositionGlobal() const
{
    return gAgent.getPosGlobalFromAgent(LLViewerCamera::getInstance()->getOrigin());
}

//-----------------------------------------------------------------------------
// calcCameraFOVZoomFactor()
//-----------------------------------------------------------------------------
F32 LLAgentCamera::calcCameraFOVZoomFactor()
{
    LLVector3 camera_offset_dir;
    camera_offset_dir.setVec(mCameraFocusOffset);

    if (mCameraMode == CAMERA_MODE_MOUSELOOK)
    {
        return 0.f;
    }
    else if (mFocusObject.notNull() && !mFocusObject->isAvatar() && !mFocusOnAvatar)
    {
        // don't FOV zoom on mostly transparent objects
        F32 obj_min_dist = 0.f;
        // Freeing the camera movement some more -KC
        if (!isDisableCameraConstraints())
            calcCameraMinDistance(obj_min_dist);
        F32 current_distance = llmax(0.001f, camera_offset_dir.magVec());

        mFocusObjectDist = obj_min_dist - current_distance;

        F32 new_fov_zoom = llclamp(mFocusObjectDist / current_distance, 0.f, 1000.f);
        return new_fov_zoom;
    }
    else // focusing on land or avatar
    {
        // keep old field of view until user changes focus explicitly
        return mCameraFOVZoomFactor;
        //return 0.f;
    }
}

//-----------------------------------------------------------------------------
// calcCameraPositionTargetGlobal()
//-----------------------------------------------------------------------------
LLVector3d LLAgentCamera::calcCameraPositionTargetGlobal(bool *hit_limit)
{
    // Compute base camera position and look-at points.
    F32         camera_land_height;
    LLVector3d  frame_center_global = !isAgentAvatarValid() ?
        gAgent.getPositionGlobal() :
        gAgent.getPosGlobalFromAgent(getAvatarRootPosition());

    bool        isConstrained = false;
    LLVector3d  head_offset;
    head_offset.setVec(mThirdPersonHeadOffset);

    LLVector3d camera_position_global;

    if (mCameraMode == CAMERA_MODE_FOLLOW && mFocusOnAvatar)
    {
        camera_position_global = gAgent.getPosGlobalFromAgent(mFollowCam.getSimulatedPosition());
    }
    else if (mCameraMode == CAMERA_MODE_MOUSELOOK)
    {
        if (!isAgentAvatarValid() || gAgentAvatarp->mDrawable.isNull())
        {
            LL_WARNS() << "Null avatar drawable!" << LL_ENDL;
            return LLVector3d::zero;
        }

        head_offset.clearVec();
        F32 fixup;
        if (gAgentAvatarp->hasPelvisFixup(fixup) && !gAgentAvatarp->isSitting())
        {
            head_offset[VZ] -= fixup;
        }
        if (gAgentAvatarp->isSitting())
        {
            head_offset.mdV[VZ] += 0.1;
        }

        if (gAgentAvatarp->isSitting() && gAgentAvatarp->getParent())
        {
            gAgentAvatarp->updateHeadOffset();
            head_offset.mdV[VX] += gAgentAvatarp->mHeadOffset.mV[VX];
            head_offset.mdV[VY] += gAgentAvatarp->mHeadOffset.mV[VY];
            head_offset.mdV[VZ] += gAgentAvatarp->mHeadOffset.mV[VZ];
            const LLMatrix4& mat = ((LLViewerObject*) gAgentAvatarp->getParent())->getRenderMatrix();
            camera_position_global = gAgent.getPosGlobalFromAgent
                                ((gAgentAvatarp->getPosition()+
                                 LLVector3(head_offset)*gAgentAvatarp->getRotation()) * mat);
        }
        else
        {
            head_offset.mdV[VZ] += gAgentAvatarp->mHeadOffset.mV[VZ];
            camera_position_global = gAgent.getPosGlobalFromAgent(gAgentAvatarp->getRenderPosition());//frame_center_global;
            head_offset = head_offset * gAgentAvatarp->getRenderRotation();
            camera_position_global = camera_position_global + head_offset;
        }
    }
    else if (mCameraMode == CAMERA_MODE_THIRD_PERSON && mFocusOnAvatar)
    {
        LLVector3 local_camera_offset;
        F32 camera_distance = 0.f;

        if (mSitCameraEnabled
            && isAgentAvatarValid()
            && gAgentAvatarp->isSitting()
            && mSitCameraReferenceObject.notNull())
        {
            // sit camera
            LLVector3 object_pos = mSitCameraReferenceObject->getRenderPosition();
            LLQuaternion object_rot = mSitCameraReferenceObject->getRenderRotation();

            LLVector3 target_pos = object_pos + (mSitCameraPos * object_rot);

            camera_position_global = gAgent.getPosGlobalFromAgent(target_pos);
        }
        else
        {
//            static LLCachedControl<F32> camera_offset_scale(gSavedSettings, "CameraOffsetScale");
//            local_camera_offset = mCameraZoomFraction * getCameraOffsetInitial() * camera_offset_scale;
// [RLVa:KB] - @setcam_eyeoffsetscale
            local_camera_offset = mCameraZoomFraction * getCameraOffsetInitial() * getCameraOffsetScale();
// [/RLVa:KB]

            // are we sitting down?
            if (isAgentAvatarValid() && gAgentAvatarp->getParent())
            {
                LLQuaternion parent_rot = ((LLViewerObject*)gAgentAvatarp->getParent())->getRenderRotation();
                // slam agent coordinate frame to proper parent local version
                LLVector3 at_axis = gAgent.getFrameAgent().getAtAxis() * parent_rot;
                at_axis.mV[VZ] = 0.f;
                at_axis.normalize();
                gAgent.resetAxes(at_axis * ~parent_rot);

                local_camera_offset = local_camera_offset * gAgent.getFrameAgent().getQuaternion() * parent_rot;
            }
            else
            {
                local_camera_offset = gAgent.getFrameAgent().rotateToAbsolute( local_camera_offset );
            }

            if (!isDisableCameraConstraints() && !mCameraCollidePlane.isExactlyZero() &&
                (!isAgentAvatarValid() || !gAgentAvatarp->isSitting()))
            {
                LLVector3 plane_normal;
                plane_normal.setVec(mCameraCollidePlane.mV);

                F32 offset_dot_norm = local_camera_offset * plane_normal;
                if (llabs(offset_dot_norm) < 0.001f)
                {
                    offset_dot_norm = 0.001f;
                }

                camera_distance = local_camera_offset.normalize();

                F32 pos_dot_norm = gAgent.getPosAgentFromGlobal(frame_center_global + head_offset) * plane_normal;

                // if agent is outside the colliding half-plane
                if (pos_dot_norm > mCameraCollidePlane.mV[VW])
                {
                    // check to see if camera is on the opposite side (inside) the half-plane
                    if (offset_dot_norm + pos_dot_norm < mCameraCollidePlane.mV[VW])
                    {
                        // diminish offset by factor to push it back outside the half-plane
                        camera_distance *= (pos_dot_norm - mCameraCollidePlane.mV[VW] - CAMERA_COLLIDE_EPSILON) / -offset_dot_norm;
                    }
                }
                else
                {
                    if (offset_dot_norm + pos_dot_norm > mCameraCollidePlane.mV[VW])
                    {
                        camera_distance *= (mCameraCollidePlane.mV[VW] - pos_dot_norm - CAMERA_COLLIDE_EPSILON) / offset_dot_norm;
                    }
                }
            }
            else
            {
                camera_distance = local_camera_offset.normalize();
            }

            mTargetCameraDistance = llmax(camera_distance, MIN_CAMERA_DISTANCE);

            if (mTargetCameraDistance != mCurrentCameraDistance)
            {
                F32 camera_lerp_amt = LLSmoothInterpolation::getInterpolant(CAMERA_ZOOM_HALF_LIFE);

                mCurrentCameraDistance = lerp(mCurrentCameraDistance, mTargetCameraDistance, camera_lerp_amt);
            }

            // Make the camera distance current
            local_camera_offset *= mCurrentCameraDistance;

            // set the global camera position
            LLVector3d camera_offset;

            camera_offset.setVec( local_camera_offset );
            camera_position_global = frame_center_global + head_offset + camera_offset;

            if (isAgentAvatarValid())
            {
                LLVector3d camera_lag_d;
                F32 lag_interp = LLSmoothInterpolation::getInterpolant(CAMERA_LAG_HALF_LIFE);
                LLVector3 target_lag;
                LLVector3 vel = gAgent.getVelocity();

                // lag by appropriate amount for flying
                F32 time_in_air = gAgentAvatarp->mTimeInAir.getElapsedTimeF32();
                if(!mCameraAnimating && gAgentAvatarp->mInAir && time_in_air > GROUND_TO_AIR_CAMERA_TRANSITION_START_TIME)
                {
                    LLVector3 frame_at_axis = gAgent.getFrameAgent().getAtAxis();
                    frame_at_axis -= projected_vec(frame_at_axis, gAgent.getReferenceUpVector());
                    frame_at_axis.normalize();

                    //transition smoothly in air mode, to avoid camera pop
                    F32 u = (time_in_air - GROUND_TO_AIR_CAMERA_TRANSITION_START_TIME) / GROUND_TO_AIR_CAMERA_TRANSITION_TIME;
                    u = llclamp(u, 0.f, 1.f);

                    lag_interp *= u;

                    if (gViewerWindow->getLeftMouseDown() && gViewerWindow->getLastPick().mObjectID == gAgentAvatarp->getID())
                    {
                        // disable camera lag when using mouse-directed steering
                        target_lag.clearVec();
                    }
                    else
                    {
                        static LLCachedControl<F32> dynamic_camera_strength(gSavedSettings, "DynamicCameraStrength");
                        target_lag = vel * dynamic_camera_strength / 30.f;
                    }

                    mCameraLag = lerp(mCameraLag, target_lag, lag_interp);

                    F32 lag_dist = mCameraLag.magVec();
                    if (lag_dist > MAX_CAMERA_LAG)
                    {
                        mCameraLag = mCameraLag * MAX_CAMERA_LAG / lag_dist;
                    }

                    // clamp camera lag so that avatar is always in front
                    F32 dot = (mCameraLag - (frame_at_axis * (MIN_CAMERA_LAG * u))) * frame_at_axis;
                    if (dot < -(MIN_CAMERA_LAG * u))
                    {
                        mCameraLag -= (dot + (MIN_CAMERA_LAG * u)) * frame_at_axis;
                    }
                }
                else
                {
                    mCameraLag = lerp(mCameraLag, LLVector3::zero, LLSmoothInterpolation::getInterpolant(0.15f));
                }

                camera_lag_d.setVec(mCameraLag);
                camera_position_global = camera_position_global - camera_lag_d;
            }
        }
    }
    else
    {
        LLVector3d focusPosGlobal = calcFocusPositionTargetGlobal();
        // camera gets pushed out later wrt mCameraFOVZoomFactor...this is "raw" value
        camera_position_global = focusPosGlobal + mCameraFocusOffset;
    }

    if (!isDisableCameraConstraints() && !gAgent.isGodlike())
    {
        LLViewerRegion* regionp = LLWorld::getInstance()->getRegionFromPosGlobal(camera_position_global);
        bool constrain = true;
        if(regionp && regionp->canManageEstate())
        {
            constrain = false;
        }
        if(constrain)
        {
            F32 max_dist = (CAMERA_MODE_CUSTOMIZE_AVATAR == mCameraMode) ? APPEARANCE_MAX_ZOOM : mDrawDistance;

            LLVector3d camera_offset = camera_position_global - gAgent.getPositionGlobal();
            F32 camera_distance = (F32)camera_offset.magVec();

            if(camera_distance > max_dist)
            {
                camera_position_global = gAgent.getPositionGlobal() + (max_dist/camera_distance)*camera_offset;
                isConstrained = true;
            }
        }

// JC - Could constrain camera based on parcel stuff here.
//          LLViewerRegion *regionp = LLWorld::getInstance()->getRegionFromPosGlobal(camera_position_global);
//
//          if (regionp && !regionp->mParcelOverlay->isBuildCameraAllowed(regionp->getPosRegionFromGlobal(camera_position_global)))
//          {
//              camera_position_global = last_position_global;
//
//              isConstrained = true;
//          }
    }

// [RLVa:KB] - Checked: RLVa-2.0.0
    if ( (RlvActions::isRlvEnabled()) && ((CAMERA_MODE_THIRD_PERSON == mCameraMode) || (CAMERA_MODE_FOLLOW == mCameraMode)) && (RlvActions::isCameraDistanceClamped()) )
    {
        m_fRlvMinDist = m_fRlvMaxDist = false;

        // Av-locked | Focus-locked | Result
        // ===================================================
        //     T     |      T       | skip focus => slam av
        //     T     |      F       | skip focus => slam av
        //     F     |      T       | skip av    => slam focus
        //     F     |      F       | clamp focus then av
        bool fCamAvDistClamped, fCamAvDistLocked = false; float nCamAvDistLimitMin, nCamAvDistLimitMax;
        if ((fCamAvDistClamped = RlvActions::getCameraAvatarDistanceLimits(nCamAvDistLimitMin, nCamAvDistLimitMax)))
            fCamAvDistLocked = nCamAvDistLimitMin == nCamAvDistLimitMax;
        bool fCamOriginDistClamped, fCamOriginDistLocked = false; float nCamOriginDistLimitMin, nCamOriginDistLimitMax;
        if ((fCamOriginDistClamped = RlvActions::getCameraOriginDistanceLimits(nCamOriginDistLimitMin, nCamOriginDistLimitMax)))
            fCamOriginDistLocked = nCamOriginDistLimitMin == nCamOriginDistLimitMax;

        // Check focus distance limits
        if ( (fCamOriginDistClamped) && (!fCamAvDistLocked) )
        {
//          const LLVector3 offsetCameraLocal = mCameraZoomFraction * getCameraOffsetInitial() * gSavedSettings.getF32("CameraOffsetScale");
// [RLVa:KB] - @setcam_eyeoffsetscale
            const LLVector3 offsetCameraLocal = mCameraZoomFraction * getCameraOffsetInitial() * getCameraOffsetScale();
// [/RLVa:KB]
            const LLVector3d offsetCamera(gAgent.getFrameAgent().rotateToAbsolute(offsetCameraLocal));
            const LLVector3d posFocusCam = frame_center_global + head_offset + offsetCamera;
            if (clampCameraPosition(camera_position_global, posFocusCam, nCamOriginDistLimitMin, nCamOriginDistLimitMax))
                isConstrained = true;
        }

        // Check avatar distance limits
        if ( (fCamAvDistClamped) && (fCamAvDistLocked || !fCamOriginDistClamped) )
        {
            const LLVector3d posAvatarCam = gAgent.getPosGlobalFromAgent( (isAgentAvatarValid()) ? gAgentAvatarp->mHeadp->getWorldPosition() : gAgent.getPositionAgent() );
            if (clampCameraPosition(camera_position_global, posAvatarCam, nCamAvDistLimitMin, nCamAvDistLimitMax))
                isConstrained = true;
        }
    }
// [/RLVa:KB]

    // <FS:humbletim> FIRE-33613: [OpenSim] [PBR] Camera cannot be located at negative Z
    F32 camera_ground_plane{ F_ALMOST_ZERO };
    // integrate OpenSimExtras.MinSimHeight into the camera ground plane calculation
    if (auto regionp = LLWorld::getInstance()->getRegionFromPosGlobal(camera_position_global))
    {
      camera_ground_plane += regionp->getMinSimHeight();
    }
    // </FS:humbletim>

    // Don't let camera go underground
    F32 camera_min_off_ground = getCameraMinOffGround();
    camera_land_height = LLWorld::getInstance()->resolveLandHeightGlobal(camera_position_global);
    F32 minZ = llmax(camera_ground_plane, camera_land_height + camera_min_off_ground); // <FS:humbletim/> FIRE-33613: [OpenSim] [PBR] Camera cannot be located at negative Z
    if (camera_position_global.mdV[VZ] < minZ)
    {
        camera_position_global.mdV[VZ] = minZ;
        isConstrained = true;
    }

    if (hit_limit)
    {
        *hit_limit = isConstrained;
    }

    return camera_position_global;
}

// [RLVa:KB] - Checked: RLVa-2.0.0
bool LLAgentCamera::allowFocusOffsetChange(const LLVector3d& offsetFocus)
{
    if (RlvActions::isCameraDistanceClamped())
    {
        if ( ((CAMERA_MODE_THIRD_PERSON == getCameraMode()) || (CAMERA_MODE_FOLLOW == getCameraMode())) && ((m_fRlvMinDist) || (m_fRlvMaxDist)) )
        {
            const LLVector3d posFocusGlobal = calcFocusPositionTargetGlobal();
            // Don't allow moving the focus offset if at minimum and moving closer (or if at maximum and moving further) to prevent camera warping
            F32 nCurDist = (F32)llabs((posFocusGlobal + mCameraFocusOffsetTarget - m_posRlvRefGlobal).magVec());
            F32 nNewDist = (F32)llabs((posFocusGlobal + offsetFocus - m_posRlvRefGlobal).magVec());
            if ( ((m_fRlvMaxDist) && (nNewDist > nCurDist)) || ((m_fRlvMinDist) && (nNewDist < nCurDist)) )
                return false;
        }
    }
    return true;
}

bool LLAgentCamera::clampCameraPosition(LLVector3d& posCamGlobal, const LLVector3d posCamRefGlobal, float nDistMin, float nDistMax)
{
    const LLVector3d offsetCamera = posCamGlobal - posCamRefGlobal;

    F32 nCamAvDist = (F32)llabs(offsetCamera.magVec()), nDistMult = NAN;
    if (nCamAvDist > nDistMax)
    {
        nDistMult = nDistMax / nCamAvDist;
        m_fRlvMaxDist = true;
    }
    else if (nCamAvDist < nDistMin)
    {
        nDistMult = nDistMin / nCamAvDist;
        m_fRlvMinDist = true;
    }

    if (!llisnan(nDistMult))
    {
        posCamGlobal = posCamRefGlobal + nDistMult * offsetCamera;
        m_posRlvRefGlobal = posCamRefGlobal;
        return true;
    }
    return false;
}
// [/RLVa:KB]

LLVector3 LLAgentCamera::getCurrentCameraOffset()
{
    return (LLViewerCamera::getInstance()->getOrigin() - getAvatarRootPosition() - mThirdPersonHeadOffset) * ~getCurrentAvatarRotation();
}

LLVector3d LLAgentCamera::getCurrentFocusOffset()
{
    return (mFocusTargetGlobal - gAgent.getPositionGlobal()) * ~getCurrentAvatarRotation();
}

LLQuaternion LLAgentCamera::getCurrentAvatarRotation()
{
    LLViewerObject* sit_object = (LLViewerObject*)gAgentAvatarp->getParent();

    LLQuaternion av_rot = gAgent.getFrameAgent().getQuaternion();
    LLQuaternion obj_rot = sit_object ? sit_object->getRenderRotation() : LLQuaternion::DEFAULT;
    return av_rot * obj_rot;
}

bool LLAgentCamera::isJoystickCameraUsed()
{
    return ((mOrbitAroundRadians != 0) || (mOrbitOverAngle != 0) || !mPanFocusDiff.isNull());
}

LLVector3 LLAgentCamera::getCameraOffsetInitial()
{
    // getCameraOffsetInitial and getFocusOffsetInitial can be called on update from idle before init()
    static LLCachedControl<LLVector3> camera_offset_initial (gSavedSettings, "CameraOffsetRearView", LLVector3());
//  return camera_offset_initial;
// [RLVa:KB] - @setcam_eyeoffset
    return ECameraPreset::CAMERA_RLV_SETCAM_VIEW != mCameraPreset ? camera_offset_initial() : convert_from_llsd<LLVector3>(mRlvCameraOffsetInitialControl->get(), TYPE_VEC3, "");
// [/RLVa:KB]
}

LLVector3d LLAgentCamera::getFocusOffsetInitial()
{
    static LLCachedControl<LLVector3d> focus_offset_initial(gSavedSettings, "FocusOffsetRearView", LLVector3d());
//  return focus_offset_initial;
// [RLVa:KB] - @setcam_focusoffset
    return ECameraPreset::CAMERA_RLV_SETCAM_VIEW != mCameraPreset ? focus_offset_initial() : convert_from_llsd<LLVector3d>(mRlvFocusOffsetInitialControl->get(), TYPE_VEC3D, "");
// [/RLVa:KB]
}

// [RLVa:KB] - @setcam_eyeoffsetscale
F32 LLAgentCamera::getCameraOffsetScale() const
{
    return gSavedSettings.getF32( (ECameraPreset::CAMERA_RLV_SETCAM_VIEW != mCameraPreset) ? "CameraOffsetScale" : "CameraOffsetScaleRLVa");
}
// [/RLVa:KB]

// <FS:Ansariel> FIRE-23470: Fix camera controls zoom glitch
//F32 LLAgentCamera::getCameraMaxZoomDistance()
F32 LLAgentCamera::getCameraMaxZoomDistance(bool allow_disabled_constraints /* = false*/)
// </FS:Ansariel>
{
    // <FS:Ansariel> FIRE-23470: Fix camera controls zoom glitch
    //// SL-14706 / SL-14885 TPV have relaxed camera constraints allowing you to mousewheeel zoom WAY out.
    //static LLCachedControl<bool> s_disable_camera_constraints(gSavedSettings, "DisableCameraConstraints", false);
    //if (s_disable_camera_constraints)
    //{
    //    return (F32)INT_MAX;
    //}
    // </FS:Ansariel>

    // Ignore "DisableCameraConstraints", we don't want to be out of draw range when we focus onto objects or avatars
    // Freeing the camera movement some more... ok, a lot -KC
    static LLCachedControl<bool> disable_constraints(gSavedSettings,"DisableCameraConstraints");
    return (allow_disabled_constraints && disable_constraints) ? INT_MAX : llmin(MAX_CAMERA_DISTANCE_FROM_OBJECT,
                 mDrawDistance - 1, // convenience, don't hit draw limit when focusing on something
                 LLWorld::getInstance()->getRegionWidthInMeters() - CAMERA_FUDGE_FROM_OBJECT);
}

LLVector3 LLAgentCamera::getAvatarRootPosition()
{
    static LLCachedControl<bool> use_hover_height(gSavedSettings, "HoverHeightAffectsCamera");
    return use_hover_height ? gAgentAvatarp->mRoot->getWorldPosition() : gAgentAvatarp->mRoot->getWorldPosition() - gAgentAvatarp->getHoverOffset();

}
//-----------------------------------------------------------------------------
// handleScrollWheel()
//-----------------------------------------------------------------------------
void LLAgentCamera::handleScrollWheel(S32 clicks)
{
    // <FS:PP> Option to disable mouse wheel for camera zooming in/out
    static LLCachedControl<bool> FSDisableMouseWheelCameraZoom(gSavedSettings, "FSDisableMouseWheelCameraZoom");

    if (mCameraMode == CAMERA_MODE_FOLLOW && getFocusOnAvatar())
    {
        // <FS:Ansariel> Option to disable mouse wheel for camera zooming in/out
        if (FSDisableMouseWheelCameraZoom)
        {
            return;
        }
        // </FS:Ansariel>

        if (!mFollowCam.getPositionLocked()) // not if the followCam position is locked in place
        {
            mFollowCam.zoom(clicks);
            if (mFollowCam.isZoomedToMinimumDistance())
            {
                changeCameraToMouselook(false);
            }
        }
    }
    else
    {
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        const F32 ROOT_ROOT_TWO = sqrt(F_SQRT2);

        // Block if camera is animating
        if (mCameraAnimating)
        {
            return;
        }

        if (selection->getObjectCount() && selection->getSelectType() == SELECT_TYPE_HUD)
        {
            F32 zoom_factor = (F32)pow(0.8, -clicks);
            cameraZoomIn(zoom_factor);
        }
        // <FS:Ansariel> Option to disable mouse wheel for camera zooming in/out
        else if (FSDisableMouseWheelCameraZoom)
        {
            return;
        }
        // </FS:Ansariel>
        else if (mFocusOnAvatar && (mCameraMode == CAMERA_MODE_THIRD_PERSON))
        {
            // <FS:Zi> Camera focus and offset with CTRL/SHIFT + Scroll wheel
            MASK mask = gKeyboard->currentMask(true);
            if (mask & MASK_SHIFT)
            {
                LLVector3d offset = gSavedSettings.getVector3d("FocusOffsetRearView");
                offset.mdV[VZ] += 0.1f * (F32)clicks;
                gSavedSettings.setVector3d("FocusOffsetRearView", offset);
                return;
            }
            else if (mask & MASK_CONTROL)
            {
                LLVector3 offset = gSavedSettings.getVector3("CameraOffsetRearView");
                offset.mV[VZ] += 0.1f * (F32)clicks;
                gSavedSettings.setVector3("CameraOffsetRearView", offset);
                return;
            }
            // </FS:Zi>

            F32 camera_offset_initial_mag = getCameraOffsetInitial().magVec();

//          F32 current_zoom_fraction = mTargetCameraDistance / (camera_offset_initial_mag * gSavedSettings.getF32("CameraOffsetScale"));
// [RLVa:KB] - @setcam_eyeoffsetscale
            F32 current_zoom_fraction = mTargetCameraDistance / (camera_offset_initial_mag * getCameraOffsetScale());
// [/RLVa:KB]
            current_zoom_fraction *= 1.f - (F32)pow(ROOT_ROOT_TWO, clicks);

// [RLVa:KB] - @setcam_eyeoffsetscale
            cameraOrbitIn(current_zoom_fraction * camera_offset_initial_mag * getCameraOffsetScale());
// [/RLVa:KB]
//          cameraOrbitIn(current_zoom_fraction * camera_offset_initial_mag * gSavedSettings.getF32("CameraOffsetScale"));
        }
        else
        {
            F32 current_zoom_fraction = (F32)mCameraFocusOffsetTarget.magVec();
            cameraOrbitIn(current_zoom_fraction * (1.f - (F32)pow(ROOT_ROOT_TWO, clicks)));
        }
    }
}


//-----------------------------------------------------------------------------
// getCameraMinOffGround()
//-----------------------------------------------------------------------------
F32 LLAgentCamera::getCameraMinOffGround()
{
    if (mCameraMode == CAMERA_MODE_MOUSELOOK)
    {
        return 0.f;
    }

    if (isDisableCameraConstraints())
    {
        return -1000.f;
    }

    return 0.5f;
}


//-----------------------------------------------------------------------------
// resetCamera()
//-----------------------------------------------------------------------------
void LLAgentCamera::resetCamera()
{
    // Remove any pitch from the avatar
    LLVector3 at = gAgent.getFrameAgent().getAtAxis();
    at.mV[VZ] = 0.f;
    at.normalize();
    gAgent.resetAxes(at);
    // have to explicitly clear field of view zoom now
    mCameraFOVZoomFactor = 0.f;

    updateCamera();
}

//-----------------------------------------------------------------------------
// changeCameraToMouselook()
//-----------------------------------------------------------------------------
void LLAgentCamera::changeCameraToMouselook(bool animate)
{
    if (!gSavedSettings.getBOOL("EnableMouselook")
// [RLVa:KB] - Checked: RLVa-2.0.0
        || ( (RlvActions::isRlvEnabled()) && (!RlvActions::canChangeToMouselook()) )
// [/RLVa:KB]

        || LLViewerJoystick::getInstance()->getOverrideCamera())
    {
        return;
    }

    // visibility changes at end of animation
    gViewerWindow->getWindow()->resetBusyCount();

    // Menus should not remain open on switching to mouselook...
    LLMenuGL::sMenuContainer->hideMenus();
    LLUI::getInstance()->clearPopups();

    // unpause avatar animation
    gAgent.unpauseAnimation();

    LLToolMgr::getInstance()->setCurrentToolset(gMouselookToolset);

    if (isAgentAvatarValid())
    {
        gAgentAvatarp->stopMotion(ANIM_AGENT_BODY_NOISE);
        gAgentAvatarp->stopMotion(ANIM_AGENT_BREATHE_ROT);
    }

    //gViewerWindow->stopGrab();
    LLSelectMgr::getInstance()->deselectAll();
//  gViewerWindow->hideCursor();
//  gViewerWindow->moveCursorToCenter();

    if (mCameraMode != CAMERA_MODE_MOUSELOOK)
    {
        gFocusMgr.setKeyboardFocus(NULL);

        updateLastCamera();
        mCameraMode = CAMERA_MODE_MOUSELOOK;
        // <FS:Zi> Animation Overrider
        AOEngine::getInstance()->inMouselook(true);
        const U32 old_flags = gAgent.getControlFlags();
        gAgent.setControlFlags(AGENT_CONTROL_MOUSELOOK);

        if (animate)
        {
            startCameraAnimation();
        }
        else
        {
            mCameraAnimating = false;
            gAgent.endAnimationUpdateUI();
        }
    }
}


//-----------------------------------------------------------------------------
// changeCameraToDefault()
//-----------------------------------------------------------------------------
void LLAgentCamera::changeCameraToDefault()
{
    if (LLViewerJoystick::getInstance()->getOverrideCamera())
    {
        return;
    }

    if (LLFollowCamMgr::getInstance()->getActiveFollowCamParams())
    {
        changeCameraToFollow();
    }
    else
    {
        changeCameraToThirdPerson();
    }
    if (gSavedSettings.getBOOL("HideUIControls"))
    {
        gViewerWindow->setUIVisibility(false);
        LLPanelStandStopFlying::getInstance()->setVisible(false);
    }
}


//-----------------------------------------------------------------------------
// changeCameraToFollow()
//-----------------------------------------------------------------------------
void LLAgentCamera::changeCameraToFollow(bool animate)
{
    if (LLViewerJoystick::getInstance()->getOverrideCamera())
    {
        return;
    }

    if (mCameraMode == CAMERA_MODE_MOUSELOOK)
	{
		gAgentAvatarp->resetSkeleton(false);
	}

    if(mCameraMode != CAMERA_MODE_FOLLOW)
    {
        if (mCameraMode == CAMERA_MODE_MOUSELOOK)
        {
            animate = false;
        }
        startCameraAnimation();

        updateLastCamera();
        mCameraMode = CAMERA_MODE_FOLLOW;
        // <FS:Zi> Animation Overrider
        AOEngine::getInstance()->inMouselook(false);

        // bang-in the current focus, position, and up vector of the follow cam
        mFollowCam.reset(mCameraPositionAgent, LLViewerCamera::getInstance()->getPointOfInterest(), LLVector3::z_axis);

        if (gBasicToolset)
        {
            LLToolMgr::getInstance()->setCurrentToolset(gBasicToolset);
        }

        if (isAgentAvatarValid())
        {
            // SL-315
            gAgentAvatarp->mPelvisp->setPosition(LLVector3::zero);
            gAgentAvatarp->startMotion( ANIM_AGENT_BODY_NOISE );
            gAgentAvatarp->startMotion( ANIM_AGENT_BREATHE_ROT );
        }

        // unpause avatar animation
        gAgent.unpauseAnimation();

        gAgent.clearControlFlags(AGENT_CONTROL_MOUSELOOK);

        if (animate)
        {
            startCameraAnimation();
        }
        else
        {
            mCameraAnimating = false;
            gAgent.endAnimationUpdateUI();
        }
    }
}

//-----------------------------------------------------------------------------
// changeCameraToThirdPerson()
//-----------------------------------------------------------------------------
void LLAgentCamera::changeCameraToThirdPerson(bool animate)
{
    if (LLViewerJoystick::getInstance()->getOverrideCamera())
    {
        return;
    }

    gViewerWindow->getWindow()->resetBusyCount();

    mCameraZoomFraction = INITIAL_ZOOM_FRACTION;

    if (isAgentAvatarValid())
    {
        if (!gAgentAvatarp->isSitting())
        {
            // SL-315
            gAgentAvatarp->mPelvisp->setPosition(LLVector3::zero);
        }
        gAgentAvatarp->startMotion(ANIM_AGENT_BODY_NOISE);
        gAgentAvatarp->startMotion(ANIM_AGENT_BREATHE_ROT);
    }

    LLVector3 at_axis;

    // unpause avatar animation
    gAgent.unpauseAnimation();

    if (mCameraMode == CAMERA_MODE_MOUSELOOK)
	{
        gAgentAvatarp->resetSkeleton(false);
	}

    if (mCameraMode != CAMERA_MODE_THIRD_PERSON)
    {
        if (gBasicToolset)
        {
            LLToolMgr::getInstance()->setCurrentToolset(gBasicToolset);
        }

        mCameraLag.clearVec();
        if (mCameraMode == CAMERA_MODE_MOUSELOOK)
        {
            mCurrentCameraDistance = MIN_CAMERA_DISTANCE;
            mTargetCameraDistance = MIN_CAMERA_DISTANCE;
            animate = false;
        }
        updateLastCamera();
        mCameraMode = CAMERA_MODE_THIRD_PERSON;
        // <FS:Zi> Animation Overrider
        AOEngine::getInstance()->inMouselook(false);
        gAgent.clearControlFlags(AGENT_CONTROL_MOUSELOOK);
    }

    // Remove any pitch from the avatar
    if (!isAgentAvatarValid() || !gAgentAvatarp->getParent())
    {
        at_axis = gAgent.getFrameAgent().getAtAxis();
        at_axis.mV[VZ] = 0.f;
        at_axis.normalize();
        gAgent.resetAxes(at_axis);
    }


    if (animate)
    {
        startCameraAnimation();
    }
    else
    {
        mCameraAnimating = false;
        gAgent.endAnimationUpdateUI();
    }
}

//-----------------------------------------------------------------------------
// changeCameraToCustomizeAvatar()
//-----------------------------------------------------------------------------
void LLAgentCamera::changeCameraToCustomizeAvatar()
{
    if (LLViewerJoystick::getInstance()->getOverrideCamera() || !isAgentAvatarValid())
    {
        return;
    }

// [RLVa:KB] - Checked: 2010-03-07 (RLVa-1.2.0c) | Modified: RLVa-1.0.0g
    if ( (rlv_handler_t::isEnabled()) && (!RlvActions::canStand()) )
    {
        return;
    }
// [/RLVa:KB]

    gAgent.standUp(); // force stand up
    gViewerWindow->getWindow()->resetBusyCount();

    if (LLSelectMgr::getInstance()->getSelection()->isAttachment())
    {
        LLSelectMgr::getInstance()->deselectAll();
    }

    if (gFaceEditToolset)
    {
        LLToolMgr::getInstance()->setCurrentToolset(gFaceEditToolset);
    }

    startCameraAnimation();

    if (mCameraMode == CAMERA_MODE_MOUSELOOK)
	{
		gAgentAvatarp->resetSkeleton(false);
	}

    if (mCameraMode != CAMERA_MODE_CUSTOMIZE_AVATAR)
    {
        updateLastCamera();
        mCameraMode = CAMERA_MODE_CUSTOMIZE_AVATAR;
        gAgent.clearControlFlags(AGENT_CONTROL_MOUSELOOK);

        gFocusMgr.setKeyboardFocus( NULL );
        gFocusMgr.setMouseCapture( NULL );
        if( gMorphView )
        {
            gMorphView->setVisible( true );
        }
        // Remove any pitch or rotation from the avatar
        LLVector3 at = gAgent.getAtAxis();
        at.mV[VZ] = 0.f;
        at.normalize();
        gAgent.resetAxes(at);

        gAgent.sendAnimationRequest(ANIM_AGENT_CUSTOMIZE, ANIM_REQUEST_START);
        gAgent.setCustomAnim(true);
        gAgentAvatarp->startMotion(ANIM_AGENT_CUSTOMIZE);
        LLMotion* turn_motion = gAgentAvatarp->findMotion(ANIM_AGENT_CUSTOMIZE);

        if (turn_motion)
        {
            // delay camera animation long enough to play through turn animation
            setAnimationDuration(turn_motion->getDuration() + CUSTOMIZE_AVATAR_CAMERA_ANIM_SLOP);
        }
    }

    LLVector3 agent_at = gAgent.getAtAxis();
    agent_at.mV[VZ] = 0.f;
    agent_at.normalize();

    // default focus point for customize avatar
    LLVector3 focus_target = isAgentAvatarValid()
        ? gAgentAvatarp->mHeadp->getWorldPosition()
        : gAgent.getPositionAgent();

    LLVector3d camera_offset(agent_at * -1.0);
    // push camera up and out from avatar
    camera_offset.mdV[VZ] = 0.1f;
    camera_offset *= CUSTOMIZE_AVATAR_CAMERA_DEFAULT_DIST;
    LLVector3d focus_target_global = gAgent.getPosGlobalFromAgent(focus_target);
    setAnimationDuration(gSavedSettings.getF32("ZoomTime"));
    setCameraPosAndFocusGlobal(focus_target_global + camera_offset, focus_target_global, gAgent.getID());
}


void LLAgentCamera::switchCameraPreset(ECameraPreset preset)
{
// [RLVa:KB] - @setcam family
    if (RlvActions::isRlvEnabled())
    {
        // Don't allow changing away from our view if an object is restricting it
        if (RlvActions::isCameraPresetLocked())
            preset = CAMERA_RLV_SETCAM_VIEW;

        if (CAMERA_RLV_SETCAM_VIEW == preset)
        {
            if (CAMERA_RLV_SETCAM_VIEW == mCameraPreset)
            {
                // Don't reset anything if our view is already current
                return;
            }
            else
            {
                // When switching to our view, copy the current values
                mRlvCameraOffsetInitialControl->setDefaultValue(convert_to_llsd(getCameraOffsetInitial()));
                mRlvFocusOffsetInitialControl->setDefaultValue(convert_to_llsd(getFocusOffsetInitial()));
                mRlvCameraOffsetScaleControl->setDefaultValue(getCameraOffsetScale());
            }
        }
    }
// [/RLVa:KB]

    //zoom is supposed to be reset for the front and group views
    mCameraZoomFraction = 1.f;

    //focusing on avatar in that case means following him on movements
    mFocusOnAvatar = true;

    mCameraPreset = preset;

    resetPanDiff();
    resetOrbitDiff();
// <FS:Chanayane> Camera roll (from Alchemy)
	resetCameraRoll();
// </FS:Chanayane>

    gSavedSettings.setU32("CameraPresetType", mCameraPreset);
}


//
// Focus point management
//

void LLAgentCamera::setAnimationDuration(F32 duration)
{
    if (mCameraAnimating)
    {
        // do not cut any existing camera animation short
        F32 animation_left = llmax(0.f, mAnimationDuration - mAnimationTimer.getElapsedTimeF32());
        mAnimationDuration = llmax(duration, animation_left);
    }
    else
    {
        mAnimationDuration = duration;
    }
}

//-----------------------------------------------------------------------------
// startCameraAnimation()
//-----------------------------------------------------------------------------
void LLAgentCamera::startCameraAnimation()
{
    mAnimationCameraStartGlobal = getCameraPositionGlobal();
    mAnimationFocusStartGlobal = mFocusGlobal;
    setAnimationDuration(gSavedSettings.getF32("ZoomTime"));
    mAnimationTimer.reset();
    mCameraAnimating = true;
}

//-----------------------------------------------------------------------------
// stopCameraAnimation()
//-----------------------------------------------------------------------------
void LLAgentCamera::stopCameraAnimation()
{
    mCameraAnimating = false;
}

void LLAgentCamera::clearFocusObject()
{
    if (mFocusObject.notNull())
    {
        startCameraAnimation();

        setFocusObject(NULL);
        mFocusObjectOffset.clearVec();
    }
}

void LLAgentCamera::setFocusObject(LLViewerObject* object)
{
    mFocusObject = object;
}

// Focus on a point, but try to keep camera position stable.
//-----------------------------------------------------------------------------
// setFocusGlobal()
//-----------------------------------------------------------------------------
void LLAgentCamera::setFocusGlobal(const LLPickInfo& pick)
{
    LLViewerObject* objectp = gObjectList.findObject(pick.mObjectID);

    if (objectp && pick.mGLTFNodeIndex == -1)
    {
        // focus on object plus designated offset
        // which may or may not be same as pick.mPosGlobal
        setFocusGlobal(objectp->getPositionGlobal() + LLVector3d(pick.mObjectOffset), pick.mObjectID);
    }
    else
    {
        // focus directly on point where user clicked
        setFocusGlobal(pick.mPosGlobal, pick.mObjectID);
    }
}


void LLAgentCamera::setFocusGlobal(const LLVector3d& focus, const LLUUID &object_id)
{
    setFocusObject(gObjectList.findObject(object_id));
    LLVector3d old_focus = mFocusTargetGlobal;
    LLViewerObject *focus_obj = mFocusObject;

    // if focus has changed
    if (old_focus != focus)
    {
        if (focus.isExactlyZero())
        {
            if (isAgentAvatarValid())
            {
                mFocusTargetGlobal = gAgent.getPosGlobalFromAgent(gAgentAvatarp->mHeadp->getWorldPosition());
            }
            else
            {
                mFocusTargetGlobal = gAgent.getPositionGlobal();
            }
            mCameraFocusOffsetTarget = getCameraPositionGlobal() - mFocusTargetGlobal;
            mCameraFocusOffset = mCameraFocusOffsetTarget;
            setLookAt(LOOKAT_TARGET_CLEAR);
        }
        else
        {
            mFocusTargetGlobal = focus;
            if (!focus_obj)
            {
                mCameraFOVZoomFactor = 0.f;
            }

            mCameraFocusOffsetTarget = gAgent.getPosGlobalFromAgent(mCameraVirtualPositionAgent) - mFocusTargetGlobal;

            startCameraAnimation();

            if (focus_obj)
            {
                if (focus_obj->isAvatar())
                {
                    setLookAt(LOOKAT_TARGET_FOCUS, focus_obj);
                }
                else
                {
                    setLookAt(LOOKAT_TARGET_FOCUS, focus_obj, (gAgent.getPosAgentFromGlobal(focus) - focus_obj->getRenderPosition()) * ~focus_obj->getRenderRotation());
                }
            }
            else
            {
                setLookAt(LOOKAT_TARGET_FOCUS, NULL, gAgent.getPosAgentFromGlobal(mFocusTargetGlobal));
            }
        }
    }
    else // focus == mFocusTargetGlobal
    {
        if (focus.isExactlyZero())
        {
            if (isAgentAvatarValid())
            {
                mFocusTargetGlobal = gAgent.getPosGlobalFromAgent(gAgentAvatarp->mHeadp->getWorldPosition());
            }
            else
            {
                mFocusTargetGlobal = gAgent.getPositionGlobal();
            }
        }
        mCameraFocusOffsetTarget = (getCameraPositionGlobal() - mFocusTargetGlobal) / (1.f + mCameraFOVZoomFactor);;
        mCameraFocusOffset = mCameraFocusOffsetTarget;
    }

    if (mFocusObject.notNull())
    {
        // for attachments, make offset relative to avatar, not the attachment
        if (mFocusObject->isAttachment())
        {
            while (mFocusObject.notNull() && !mFocusObject->isAvatar())
            {
                mFocusObject = (LLViewerObject*) mFocusObject->getParent();
            }
            setFocusObject((LLViewerObject*)mFocusObject);
        }
        updateFocusOffset();
    }
}

// Used for avatar customization
//-----------------------------------------------------------------------------
// setCameraPosAndFocusGlobal()
//-----------------------------------------------------------------------------
void LLAgentCamera::setCameraPosAndFocusGlobal(const LLVector3d& camera_pos, const LLVector3d& focus, const LLUUID &object_id)
{
    LLVector3d old_focus = mFocusTargetGlobal.isExactlyZero() ? focus : mFocusTargetGlobal;

    F64 focus_delta_squared = (old_focus - focus).magVecSquared();
    const F64 ANIM_EPSILON_SQUARED = 0.0001;
    if (focus_delta_squared > ANIM_EPSILON_SQUARED)
    {
        startCameraAnimation();
    }

    //LLViewerCamera::getInstance()->setOrigin( gAgent.getPosAgentFromGlobal( camera_pos ) );
    setFocusObject(gObjectList.findObject(object_id));
    mFocusTargetGlobal = focus;
    mCameraFocusOffsetTarget = camera_pos - focus;
    mCameraFocusOffset = mCameraFocusOffsetTarget;

    if (mFocusObject)
    {
        if (mFocusObject->isAvatar())
        {
            setLookAt(LOOKAT_TARGET_FOCUS, mFocusObject);
        }
        else
        {
            setLookAt(LOOKAT_TARGET_FOCUS, mFocusObject, (gAgent.getPosAgentFromGlobal(focus) - mFocusObject->getRenderPosition()) * ~mFocusObject->getRenderRotation());
        }
    }
    else
    {
        setLookAt(LOOKAT_TARGET_FOCUS, NULL, gAgent.getPosAgentFromGlobal(mFocusTargetGlobal));
    }

    if (mCameraAnimating)
    {
        const F64 ANIM_METERS_PER_SECOND = 10.0;
        const F64 MIN_ANIM_SECONDS = 0.5;
        //const F64 MAX_ANIM_SECONDS = 10.0;
        F64 MAX_ANIM_SECONDS = 1.0; // Andromeda radar cam patch, make camming faster
        F64 anim_duration = llmax( MIN_ANIM_SECONDS, sqrt(focus_delta_squared) / ANIM_METERS_PER_SECOND );
        anim_duration = llmin( anim_duration, MAX_ANIM_SECONDS );
        setAnimationDuration( (F32)anim_duration );
    }

    updateFocusOffset();
}

//-----------------------------------------------------------------------------
// setSitCamera()
//-----------------------------------------------------------------------------
void LLAgentCamera::setSitCamera(const LLUUID &object_id, const LLVector3 &camera_pos, const LLVector3 &camera_focus)
{
    bool camera_enabled = !object_id.isNull();

    if (camera_enabled)
    {
        LLViewerObject *reference_object = gObjectList.findObject(object_id);
        if (reference_object)
        {
            //convert to root object relative?
            mSitCameraPos = camera_pos;
            mSitCameraFocus = camera_focus;
            mSitCameraReferenceObject = reference_object;
            mSitCameraEnabled = true;
        }
    }
    else
    {
        mSitCameraPos.clearVec();
        mSitCameraFocus.clearVec();
        mSitCameraReferenceObject = NULL;
        mSitCameraEnabled = false;
    }
}

//-----------------------------------------------------------------------------
// setFocusOnAvatar()
//-----------------------------------------------------------------------------
void LLAgentCamera::setFocusOnAvatar(bool focus_on_avatar, bool animate, bool reset_axes)
{
    if (focus_on_avatar != mFocusOnAvatar)
    {
        if (animate)
        {
            startCameraAnimation();
        }
        else
        {
            stopCameraAnimation();
        }
    }

    //RN: when focused on the avatar, we're not "looking" at it
    // looking implies intent while focusing on avatar means
    // you're just walking around with a camera on you...eesh.
    if (!mFocusOnAvatar && focus_on_avatar && reset_axes)
    {
        setFocusGlobal(LLVector3d::zero);
        mCameraFOVZoomFactor = 0.f;
        if (mCameraMode == CAMERA_MODE_THIRD_PERSON)
        {
            LLVector3 at_axis;
            if (!isAgentAvatarValid() || !gAgentAvatarp->getParent())
            {
                // In case of front view rotate agent to look into direction opposite to camera
                // In case of rear view rotate agent into diraction same as camera, e t c
                LLVector3 vect = getCameraOffsetInitial();
                F32 rotxy = F32(atan2(vect.mV[VY], vect.mV[VX]));

                LLCoordFrame frameCamera = *((LLCoordFrame*)LLViewerCamera::getInstance());
                // front view angle rotxy is zero, rear view rotxy angle is 180, compensate
                frameCamera.yaw((180 * DEG_TO_RAD) - rotxy);
                at_axis = frameCamera.getAtAxis();
                at_axis.mV[VZ] = 0.f;
                at_axis.normalize();
                gAgent.resetAxes(at_axis);
                gAgent.yaw(0);
            }
        }
    }
    // unlocking camera from avatar
    else if (mFocusOnAvatar && !focus_on_avatar)
    {
        // keep camera focus point consistent, even though it is now unlocked
        setFocusGlobal(gAgent.getPositionGlobal() + calcThirdPersonFocusOffset(), gAgent.getID());
        mAllowChangeToFollow = false;
    }

    mFocusOnAvatar = focus_on_avatar;
}


bool LLAgentCamera::setLookAt(ELookAtType target_type, LLViewerObject *object, LLVector3 position)
{
    static LLCachedControl<bool> isLocalPrivate(gSavedSettings, "PrivateLocalLookAtTarget", false);

    // AO, set to absolutely nothing if local lookats are disabled.
    if(isLocalPrivate)
    {
            position.clearVec();
            target_type = LOOKAT_TARGET_NONE;
            object = gAgentAvatarp;
    }

    else if(object && object->isAttachment())
    {
        LLViewerObject* parent = object;
        while(parent)
        {
            if (parent == gAgentAvatarp)
            {
                // looking at an attachment on ourselves, which we don't want to do
                object = gAgentAvatarp;
                position.clearVec();
            }
            parent = (LLViewerObject*)parent->getParent();
        }
    }

    if(!mLookAt || mLookAt->isDead())
    {
        mLookAt = (LLHUDEffectLookAt *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_LOOKAT);
        mLookAt->setSourceObject(gAgentAvatarp);
    }

    return mLookAt->setLookAt(target_type, object, position);
}

//-----------------------------------------------------------------------------
// lookAtLastChat()
//-----------------------------------------------------------------------------
void LLAgentCamera::lookAtLastChat()
{
    // Block if camera is animating or not in normal third person camera mode
    if (mCameraAnimating || !cameraThirdPerson())
    {
        return;
    }

    LLViewerObject *chatter = gObjectList.findObject(gAgent.getLastChatter());
    if (!chatter)
    {
        return;
    }

    LLVector3 delta_pos;
    if (chatter->isAvatar())
    {
        LLVOAvatar *chatter_av = (LLVOAvatar*)chatter;
        if (isAgentAvatarValid() && chatter_av->mHeadp)
        {
            delta_pos = chatter_av->mHeadp->getWorldPosition() - gAgentAvatarp->mHeadp->getWorldPosition();
        }
        else
        {
            delta_pos = chatter->getPositionAgent() - gAgent.getPositionAgent();
        }
        delta_pos.normalize();

        gAgent.setControlFlags(AGENT_CONTROL_STOP);

        changeCameraToThirdPerson();

        LLVector3 new_camera_pos = gAgentAvatarp->mHeadp->getWorldPosition();
        LLVector3 left = delta_pos % LLVector3::z_axis;
        left.normalize();
        LLVector3 up = left % delta_pos;
        up.normalize();
        new_camera_pos -= delta_pos * 0.4f;
        new_camera_pos += left * 0.3f;
        new_camera_pos += up * 0.2f;

        setFocusOnAvatar(false, false);

        if (chatter_av->mHeadp)
        {
            setFocusGlobal(gAgent.getPosGlobalFromAgent(chatter_av->mHeadp->getWorldPosition()), gAgent.getLastChatter());
            mCameraFocusOffsetTarget = gAgent.getPosGlobalFromAgent(new_camera_pos) - gAgent.getPosGlobalFromAgent(chatter_av->mHeadp->getWorldPosition());
        }
        else
        {
            setFocusGlobal(chatter->getPositionGlobal(), gAgent.getLastChatter());
            mCameraFocusOffsetTarget = gAgent.getPosGlobalFromAgent(new_camera_pos) - chatter->getPositionGlobal();
        }
    }
    else
    {
        delta_pos = chatter->getRenderPosition() - gAgent.getPositionAgent();
        delta_pos.normalize();

        gAgent.setControlFlags(AGENT_CONTROL_STOP);

        changeCameraToThirdPerson();

        LLVector3 new_camera_pos = gAgentAvatarp->mHeadp->getWorldPosition();
        LLVector3 left = delta_pos % LLVector3::z_axis;
        left.normalize();
        LLVector3 up = left % delta_pos;
        up.normalize();
        new_camera_pos -= delta_pos * 0.4f;
        new_camera_pos += left * 0.3f;
        new_camera_pos += up * 0.2f;

        setFocusOnAvatar(false, false);

        setFocusGlobal(chatter->getPositionGlobal(), gAgent.getLastChatter());
        mCameraFocusOffsetTarget = gAgent.getPosGlobalFromAgent(new_camera_pos) - chatter->getPositionGlobal();
    }
}

bool LLAgentCamera::isfollowCamLocked()
{
    return mFollowCam.getPositionLocked();
}

bool LLAgentCamera::setPointAt(EPointAtType target_type, LLViewerObject *object, LLVector3 position)
{
    // <FS:Ansariel> Remember the current object point pointed at - we might need it later
    mPointAtObject = object;

    // <FS:Ansariel> Private point at
    static LLCachedControl<bool> private_pointat(gSavedSettings, "PrivatePointAtTarget", false);
    if (private_pointat)
    {
        if (mPointAt && !mPointAt->isDead())
        {
            mPointAt->clearPointAtTarget();
            mPointAt->markDead();
        }

        return false;
    }
    // </FS:Ansariel>

    // disallow pointing at attachments and avatars
    //this is the editing arm motion
    if (object && (object->isAttachment() || object->isAvatar()))
    {
        return false;
    }
    if (!mPointAt || mPointAt->isDead())
    {
        mPointAt = (LLHUDEffectPointAt *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_POINTAT);
        mPointAt->setSourceObject(gAgentAvatarp);
    }
    return mPointAt->setPointAt(target_type, object, position);
}

void LLAgentCamera::rotateToInitSitRot()
{
    gAgent.rotate(~gAgent.getFrameAgent().getQuaternion());
    gAgent.rotate(mInitSitRot);
}

void LLAgentCamera::resetCameraZoomFraction()
{
    mCameraZoomFraction = INITIAL_ZOOM_FRACTION;
}

ELookAtType LLAgentCamera::getLookAtType()
{
    if (mLookAt)
    {
        return mLookAt->getLookAtType();
    }
    return LOOKAT_TARGET_NONE;
}

EPointAtType LLAgentCamera::getPointAtType()
{
    if (mPointAt)
    {
        return mPointAt->getPointAtType();
    }
    return POINTAT_TARGET_NONE;
}

void LLAgentCamera::clearGeneralKeys()
{
    mAtKey              = 0;
    mWalkKey            = 0;
    mLeftKey            = 0;
    mUpKey              = 0;
    mYawKey             = 0.f;
    mPitchKey           = 0.f;
}

void LLAgentCamera::clearOrbitKeys()
{
    mOrbitLeftKey       = 0.f;
    mOrbitRightKey      = 0.f;
    mOrbitUpKey         = 0.f;
    mOrbitDownKey       = 0.f;
    mOrbitInKey         = 0.f;
    mOrbitOutKey        = 0.f;
// <FS:Chanayane> Camera roll (from Alchemy)
    mRollLeftKey        = 0.f;
    mRollRightKey       = 0.f;
// </FS:Chanayane>
}

void LLAgentCamera::clearPanKeys()
{
    mPanRightKey        = 0.f;
    mPanLeftKey         = 0.f;
    mPanUpKey           = 0.f;
    mPanDownKey         = 0.f;
    mPanInKey           = 0.f;
    mPanOutKey          = 0.f;
}

// static
S32 LLAgentCamera::directionToKey(S32 direction)
{
    if (direction > 0) return 1;
    if (direction < 0) return -1;
    return 0;
}

// <FS:Ansariel> FIRE-7758: Save/load camera position feature
void LLAgentCamera::storeCameraPosition()
{
    gSavedPerAccountSettings.setVector3d("FSStoredCameraPos", getCameraPositionGlobal());

    // get a vector pointing forward from the camera view manually, getFocusTargetGlobal() will
    // not return useful values if the camera is in flycam mode or was just switched out of
    // flycam  mode and not repositioned after
    LLVector3d forward = LLVector3d(1.0, 0.0, 0.0) * LLViewerCamera::getInstance()->getQuaternion() + getCameraPositionGlobal();
    gSavedPerAccountSettings.setVector3d("FSStoredCameraFocus", forward);
// <FS:Chanayane> Camera roll (from Alchemy)
	gSavedPerAccountSettings.setF32("ALStoredCameraRoll", mRollAngle);
// </FS:Chanayane>

    LLUUID stored_camera_focus_object_id = LLUUID::null;
    if (mFocusObject)
    {
        stored_camera_focus_object_id = mFocusObject->getID();
    }
    gSavedPerAccountSettings.setString("FSStoredCameraFocusObjectId", stored_camera_focus_object_id.asString());
}

void LLAgentCamera::loadCameraPosition()
{
    LLVector3d stored_camera_pos = gSavedPerAccountSettings.getVector3d("FSStoredCameraPos");
    LLVector3d stored_camera_focus = gSavedPerAccountSettings.getVector3d("FSStoredCameraFocus");
// <FS:Chanayane> Camera roll (from Alchemy)
    F32 stored_camera_roll = gSavedPerAccountSettings.getF32("ALStoredCameraRoll");
// </FS:Chanayane>
    LLUUID stored_camera_focus_object_id = LLUUID(gSavedPerAccountSettings.getString("FSStoredCameraFocusObjectId"));

    F32 renderFarClip = gSavedSettings.getF32("RenderFarClip");
    F32 far_clip_squared = renderFarClip * renderFarClip;

    if (stored_camera_pos.isNull())
    {
        FSCommon::report_to_nearby_chat(LLTrans::getString("LoadCameraPositionNoneSaved"));
        return;
    }

    if (dist_vec_squared(gAgent.getPositionGlobal(), stored_camera_pos) > far_clip_squared)
    {
        FSCommon::report_to_nearby_chat(LLTrans::getString("LoadCameraPositionOutsideDrawDistance"));
        return;
    }

    // switch off flycam mode if needed
    if (LLViewerJoystick::getInstance()->getOverrideCamera())
    {
        handle_toggle_flycam();

        // exiting from flycam usually keeps the camera where it is but here we want it to actually move
        LLViewerJoystick::getInstance()->setCameraNeedsUpdate(true);
    }

    unlockView();
    setCameraPosAndFocusGlobal(stored_camera_pos, stored_camera_focus, stored_camera_focus_object_id);
// <FS:Chanayane> Camera roll (from Alchemy)
	mRollAngle = stored_camera_roll;
// </FS:Chanayane>
}
// </FS:Ansariel> FIRE-7758: Save/load camera position feature

// EOF
