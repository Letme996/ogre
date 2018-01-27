/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2017 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreStableHeaders.h"

#include "OgreSceneFormatImporter.h"
#include "OgreSceneManager.h"
#include "OgreRoot.h"

#include "OgreLwString.h"

#include "OgreItem.h"
#include "OgreMesh2.h"
#include "OgreEntity.h"
#include "OgreHlms.h"

#include "OgreHlmsPbs.h"
#include "InstantRadiosity/OgreInstantRadiosity.h"
#include "OgreIrradianceVolume.h"

#include "OgreMeshSerializer.h"
#include "OgreMesh2Serializer.h"
#include "OgreFileSystemLayer.h"

#include "OgreLogManager.h"

#include "rapidjson/document.h"

namespace Ogre
{
    SceneFormatImporter::SceneFormatImporter( Root *root, SceneManager *sceneManager ) :
        SceneFormatBase( root, sceneManager ),
        mInstantRadiosity( 0 ),
        mIrradianceVolume( 0 )
    {
    }
    //-----------------------------------------------------------------------------------
    SceneFormatImporter::~SceneFormatImporter()
    {
        destroyInstantRadiosity();
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::destroyInstantRadiosity(void)
    {
        if( mIrradianceVolume )
        {
            HlmsPbs *hlmsPbs = getPbs();
            if( hlmsPbs && hlmsPbs->getIrradianceVolume() == mIrradianceVolume )
                hlmsPbs->setIrradianceVolume( 0 );

            delete mIrradianceVolume;
            mIrradianceVolume = 0;
        }

        delete mInstantRadiosity;
        mInstantRadiosity = 0;
    }
    //-----------------------------------------------------------------------------------
    HlmsPbs* SceneFormatImporter::getPbs(void) const
    {
        HlmsManager *hlmsManager = mRoot->getHlmsManager();
        Hlms *hlms = hlmsManager->getHlms( "pbs" );
        return dynamic_cast<HlmsPbs*>( hlms );
    }
    //-----------------------------------------------------------------------------------
    Light::LightTypes SceneFormatImporter::parseLightType( const char *value )
    {
        for( size_t i=0; i<Light::NUM_LIGHT_TYPES+1u; ++i )
        {
            if( !strcmp( value, c_lightTypes[i] ) )
                return static_cast<Light::LightTypes>( i );
        }

        return Light::LT_DIRECTIONAL;
    }
    //-----------------------------------------------------------------------------------
    inline float SceneFormatImporter::decodeFloat( const rapidjson::Value &jsonValue )
    {
        union MyUnion
        {
            float   f32;
            uint32  u32;
        };

        MyUnion myUnion;
        myUnion.u32 = jsonValue.GetUint();
        return myUnion.f32;
    }
    //-----------------------------------------------------------------------------------
    inline double SceneFormatImporter::decodeDouble( const rapidjson::Value &jsonValue )
    {
        union MyUnion
        {
            double  f64;
            uint64  u64;
        };

        MyUnion myUnion;
        myUnion.u64 = jsonValue.GetUint64();
        return myUnion.f64;
    }
    //-----------------------------------------------------------------------------------
    inline Vector2 SceneFormatImporter::decodeVector2Array( const rapidjson::Value &jsonArray )
    {
        Vector2 retVal( Vector2::ZERO );

        const rapidjson::SizeType arraySize = std::min( 2u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Vector3 SceneFormatImporter::decodeVector3Array( const rapidjson::Value &jsonArray )
    {
        Vector3 retVal( Vector3::ZERO );

        const rapidjson::SizeType arraySize = std::min( 3u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Vector4 SceneFormatImporter::decodeVector4Array( const rapidjson::Value &jsonArray )
    {
        Vector4 retVal( Vector4::ZERO );

        const rapidjson::SizeType arraySize = std::min( 4u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Quaternion SceneFormatImporter::decodeQuaternionArray( const rapidjson::Value &jsonArray )
    {
        Quaternion retVal( Quaternion::IDENTITY );

        const rapidjson::SizeType arraySize = std::min( 4u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline ColourValue SceneFormatImporter::decodeColourValueArray( const rapidjson::Value &jsonArray )
    {
        ColourValue retVal( ColourValue::Black );

        const rapidjson::SizeType arraySize = std::min( 4u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Aabb SceneFormatImporter::decodeAabbArray( const rapidjson::Value &jsonArray,
                                                      const Aabb &defaultValue )
    {
        Aabb retVal( defaultValue );

        if( jsonArray.Size() == 2u )
        {
            retVal.mCenter = decodeVector3Array( jsonArray[0] );
            retVal.mHalfSize = decodeVector3Array( jsonArray[1] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importNode( const rapidjson::Value &nodeValue, Node *node )
    {
        rapidjson::Value::ConstMemberIterator  itor;

        itor = nodeValue.FindMember( "position" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setPosition( decodeVector3Array( itor->value ) );

        itor = nodeValue.FindMember( "rotation" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setOrientation( decodeQuaternionArray( itor->value ) );

        itor = nodeValue.FindMember( "scale" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setScale( decodeVector3Array( itor->value ) );

        itor = nodeValue.FindMember( "inherit_orientation" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsBool() )
            node->setInheritOrientation( itor->value.GetBool() );

        itor = nodeValue.FindMember( "inherit_scale" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsBool() )
            node->setInheritScale( itor->value.GetBool() );
    }
    //-----------------------------------------------------------------------------------
    SceneNode* SceneFormatImporter::importSceneNode( const rapidjson::Value &sceneNodeValue,
                                                     uint32 nodeIdx,
                                                     const rapidjson::Value &sceneNodesJson )
    {
        SceneNode *sceneNode = 0;

        rapidjson::Value::ConstMemberIterator itTmp = sceneNodeValue.FindMember( "node" );
        if( itTmp != sceneNodeValue.MemberEnd() && itTmp->value.IsObject() )
        {
            const rapidjson::Value &nodeValue = itTmp->value;

            bool isStatic = false;
            uint32 parentIdx = nodeIdx;

            itTmp = nodeValue.FindMember( "parent_id" );
            if( itTmp != nodeValue.MemberEnd() && itTmp->value.IsUint() )
                parentIdx = itTmp->value.GetUint();

            itTmp = nodeValue.FindMember( "is_static" );
            if( itTmp != nodeValue.MemberEnd() && itTmp->value.IsBool() )
                isStatic = itTmp->value.GetBool();

            const SceneMemoryMgrTypes sceneNodeType = isStatic ? SCENE_STATIC : SCENE_DYNAMIC;

            if( parentIdx != nodeIdx )
            {
                SceneNode *parentNode = 0;
                IndexToSceneNodeMap::const_iterator parentNodeIt = mCreatedSceneNodes.find( parentIdx );
                if( parentNodeIt == mCreatedSceneNodes.end() )
                {
                    //Our parent node will be created after us. Initialize it now.
                    if( parentIdx < sceneNodesJson.Size() &&
                        sceneNodesJson[parentIdx].IsObject() )
                    {
                        parentNode = importSceneNode( sceneNodesJson[parentIdx], parentIdx,
                                                      sceneNodesJson );
                    }

                    if( !parentNode )
                    {
                        OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND,
                                     "Node " + StringConverter::toString( nodeIdx ) + " is child of " +
                                     StringConverter::toString( parentIdx ) +
                                     " but we could not find it or create it. This file is malformed.",
                                     "SceneFormatImporter::importSceneNode" );
                    }
                }
                else
                {
                    //Parent was already created
                    parentNode = parentNodeIt->second;
                }

                sceneNode = parentNode->createChildSceneNode( sceneNodeType );
            }
            else
            {
                //Has no parent. Could be root scene node,
                //or a loose node whose parent wasn't exported.
                bool isRootNode = false;
                itTmp = sceneNodeValue.FindMember( "is_root_node" );
                if( itTmp != sceneNodeValue.MemberEnd() && itTmp->value.IsBool() )
                    isRootNode = itTmp->value.GetBool();

                if( isRootNode )
                    sceneNode = mSceneManager->getRootSceneNode( sceneNodeType );
                else
                    sceneNode = mSceneManager->createSceneNode( sceneNodeType );
            }

            importNode( nodeValue, sceneNode );

            mCreatedSceneNodes[nodeIdx] = sceneNode;
        }
        else
        {
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND,
                         "Object 'node' must be present in a scene_node. SceneNode: " +
                         StringConverter::toString( nodeIdx ) + " File: " + mFilename,
                         "SceneFormatImporter::importSceneNodes" );
        }

        return sceneNode;
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSceneNodes( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstValueIterator begin = json.Begin();
        rapidjson::Value::ConstValueIterator itor = begin;
        rapidjson::Value::ConstValueIterator end  = json.End();

        while( itor != end )
        {
            const size_t nodeIdx = itor - begin;
            if( itor->IsObject() &&
                mCreatedSceneNodes.find( nodeIdx ) == mCreatedSceneNodes.end() )
            {
                importSceneNode( *itor, nodeIdx, json );
            }

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importMovableObject( const rapidjson::Value &movableObjectValue,
                                                   MovableObject *movableObject )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = movableObjectValue.FindMember( "name" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsString() )
            movableObject->setName( tmpIt->value.GetString() );

        tmpIt = movableObjectValue.FindMember( "parent_node_id" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
        {
            uint32 nodeId = tmpIt->value.GetUint();
            IndexToSceneNodeMap::const_iterator itNode = mCreatedSceneNodes.find( nodeId );
            if( itNode != mCreatedSceneNodes.end() )
                itNode->second->attachObject( movableObject );
            else
            {
                LogManager::getSingleton().logMessage( "WARNING: MovableObject references SceneNode " +
                                                       StringConverter::toString( nodeId ) +
                                                       " which does not exist or couldn't be created" );
            }
        }

        tmpIt = movableObjectValue.FindMember( "render_queue" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
        {
            uint32 rqId = tmpIt->value.GetUint();
            movableObject->setRenderQueueGroup( rqId );
        }

        tmpIt = movableObjectValue.FindMember( "local_aabb" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            movableObject->setLocalAabb( decodeAabbArray( tmpIt->value,
                                                          movableObject->getLocalAabb() ) );
        }

        ObjectData &objData = movableObject->_getObjectData();

        tmpIt = movableObjectValue.FindMember( "local_radius" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mLocalRadius[objData.mIndex] = decodeFloat( tmpIt->value );

        tmpIt = movableObjectValue.FindMember( "rendering_distance" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            movableObject->setRenderingDistance( decodeFloat( tmpIt->value ) );

        //Decode raw flag values
        tmpIt = movableObjectValue.FindMember( "visibility_flags" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mVisibilityFlags[objData.mIndex] = tmpIt->value.GetUint();
        tmpIt = movableObjectValue.FindMember( "query_flags" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mQueryFlags[objData.mIndex] = tmpIt->value.GetUint();
        tmpIt = movableObjectValue.FindMember( "light_mask" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mLightMask[objData.mIndex] = tmpIt->value.GetUint();
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importRenderable( const rapidjson::Value &renderableValue,
                                                Renderable *renderable )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = renderableValue.FindMember( "custom_parameters" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsObject() )
        {
            rapidjson::Value::ConstMemberIterator itor = tmpIt->value.MemberBegin();
            rapidjson::Value::ConstMemberIterator end  = tmpIt->value.MemberEnd();

            while( itor != end )
            {
                if( itor->name.IsUint() && itor->value.IsArray() )
                {
                    const uint32 idxCustomParam = itor->name.GetUint();
                    renderable->setCustomParameter( idxCustomParam, decodeVector4Array( itor->value ) );
                }

                ++itor;
            }
        }

        bool isV1Material = false;
        tmpIt = renderableValue.FindMember( "is_v1_material" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            isV1Material = tmpIt->value.GetBool();

        tmpIt = renderableValue.FindMember( "datablock" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsString() )
        {
            if( !isV1Material )
                renderable->setDatablock( tmpIt->value.GetString() );
            else
            {
                renderable->setDatablockOrMaterialName(
                            tmpIt->value.GetString(),
                            ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME );
            }
        }

        tmpIt = renderableValue.FindMember( "custom_parameter" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsUint() )
            renderable->mCustomParameter = static_cast<uint8>( tmpIt->value.GetUint() );

        tmpIt = renderableValue.FindMember( "render_queue_sub_group" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsUint() )
            renderable->setRenderQueueSubGroup( static_cast<uint8>( tmpIt->value.GetUint() ) );

        tmpIt = renderableValue.FindMember( "polygon_mode_overrideable" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setPolygonModeOverrideable( tmpIt->value.GetBool() );

        tmpIt = renderableValue.FindMember( "use_identity_view" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setUseIdentityView( tmpIt->value.GetBool() );

        tmpIt = renderableValue.FindMember( "use_identity_projection" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setUseIdentityProjection( tmpIt->value.GetBool() );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSubItem( const rapidjson::Value &subentityValue, SubItem *subItem )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;
        tmpIt = subentityValue.FindMember( "renderable" );
        if( tmpIt != subentityValue.MemberEnd() && tmpIt->value.IsObject() )
            importRenderable( tmpIt->value, subItem );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSubEntity( const rapidjson::Value &subEntityValue,
                                               v1::SubEntity *subEntity )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;
        tmpIt = subEntityValue.FindMember( "renderable" );
        if( tmpIt != subEntityValue.MemberEnd() && tmpIt->value.IsObject() )
            importRenderable( tmpIt->value, subEntity );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importItem( const rapidjson::Value &entityValue )
    {
        String meshName, resourceGroup;

        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = entityValue.FindMember( "mesh" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsString() )
            meshName = tmpIt->value.GetString();

        resourceGroup = "SceneFormatImporter";
//        tmpIt = entityValue.FindMember( "mesh_resource_group" );
//        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsString() )
//            resourceGroup = tmpIt->value.GetString();

//        if( resourceGroup.empty() )
//            resourceGroup = ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME;

        bool isStatic = false;
        rapidjson::Value const *movableObjectValue = 0;

        tmpIt = entityValue.FindMember( "movable_object" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsObject() )
        {
            movableObjectValue = &tmpIt->value;

            tmpIt = movableObjectValue->FindMember( "is_static" );
            if( tmpIt != movableObjectValue->MemberEnd() && tmpIt->value.IsBool() )
                isStatic = tmpIt->value.GetBool();
        }

        const SceneMemoryMgrTypes sceneNodeType = isStatic ? SCENE_STATIC : SCENE_DYNAMIC;

        Item *item = mSceneManager->createItem( meshName, resourceGroup, sceneNodeType );

        if( movableObjectValue )
            importMovableObject( *movableObjectValue, item );

        tmpIt = entityValue.FindMember( "sub_items" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            const rapidjson::Value &subItemsArray = tmpIt->value;
            const size_t numSubItems = std::min<size_t>( item->getNumSubItems(),
                                                         subItemsArray.Size() );
            for( size_t i=0; i<numSubItems; ++i )
            {
                const rapidjson::Value &subentityValue = subItemsArray[i];

                if( subentityValue.IsObject() )
                    importSubItem( subentityValue, item->getSubItem( i ) );
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importItems( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstValueIterator itor = json.Begin();
        rapidjson::Value::ConstValueIterator end  = json.End();

        while( itor != end )
        {
            if( itor->IsObject() )
                importItem( *itor );

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importEntity( const rapidjson::Value &entityValue )
    {
        String meshName, resourceGroup;

        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = entityValue.FindMember( "mesh" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsString() )
            meshName = tmpIt->value.GetString();

        resourceGroup = "SceneFormatImporter";
//        tmpIt = entityValue.FindMember( "mesh_resource_group" );
//        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsString() )
//            resourceGroup = tmpIt->value.GetString();

//        if( resourceGroup.empty() )
//            resourceGroup = ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME;

        bool isStatic = false;
        rapidjson::Value const *movableObjectValue = 0;

        tmpIt = entityValue.FindMember( "movable_object" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsObject() )
        {
            movableObjectValue = &tmpIt->value;

            tmpIt = movableObjectValue->FindMember( "is_static" );
            if( tmpIt != movableObjectValue->MemberEnd() && tmpIt->value.IsBool() )
                isStatic = tmpIt->value.GetBool();
        }

        const SceneMemoryMgrTypes sceneNodeType = isStatic ? SCENE_STATIC : SCENE_DYNAMIC;

        v1::Entity *entity = mSceneManager->createEntity( meshName, resourceGroup, sceneNodeType );

        if( movableObjectValue )
            importMovableObject( *movableObjectValue, entity );

        tmpIt = entityValue.FindMember( "sub_entities" );
        if( tmpIt != entityValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            const rapidjson::Value &subEntitiesArray = tmpIt->value;
            const size_t numSubEntities = std::min<size_t>( entity->getNumSubEntities(),
                                                            subEntitiesArray.Size() );
            for( size_t i=0; i<numSubEntities; ++i )
            {
                const rapidjson::Value &subEntityValue = subEntitiesArray[i];

                if( subEntityValue.IsObject() )
                    importSubEntity( subEntityValue, entity->getSubEntity( i ) );
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importEntities( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstValueIterator itor = json.Begin();
        rapidjson::Value::ConstValueIterator end  = json.End();

        while( itor != end )
        {
            if( itor->IsObject() )
                importEntity( *itor );

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importLight( const rapidjson::Value &lightValue )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;

        Light *light = mSceneManager->createLight();

        tmpIt = lightValue.FindMember( "movable_object" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsObject() )
        {
            const rapidjson::Value &movableObjectValue = tmpIt->value;
            importMovableObject( movableObjectValue, light );
        }

        tmpIt = lightValue.FindMember( "diffuse" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsArray() )
            light->setDiffuseColour( decodeColourValueArray( tmpIt->value ) );

        tmpIt = lightValue.FindMember( "specular" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsArray() )
            light->setSpecularColour( decodeColourValueArray( tmpIt->value ) );

        tmpIt = lightValue.FindMember( "power" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsUint() )
            light->setPowerScale( decodeFloat( tmpIt->value ) );

        tmpIt = lightValue.FindMember( "type" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsString() )
            light->setType( parseLightType( tmpIt->value.GetString() ) );

        tmpIt = lightValue.FindMember( "attenuation" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            const Vector4 rangeConstLinQuad = decodeVector4Array( tmpIt->value );
            light->setAttenuation( rangeConstLinQuad.x, rangeConstLinQuad.y,
                                   rangeConstLinQuad.z, rangeConstLinQuad.w );
        }

        tmpIt = lightValue.FindMember( "spot" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            const Vector4 innerOuterFalloffNearClip = decodeVector4Array( tmpIt->value );
            light->setSpotlightInnerAngle( Radian( innerOuterFalloffNearClip.x ) );
            light->setSpotlightOuterAngle( Radian( innerOuterFalloffNearClip.y ) );
            light->setSpotlightFalloff( innerOuterFalloffNearClip.z );
            light->setSpotlightNearClipDistance( innerOuterFalloffNearClip.w );
        }

        tmpIt = lightValue.FindMember( "affect_parent_node" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsBool() )
            light->setAffectParentNode( tmpIt->value.GetBool() );

        tmpIt = lightValue.FindMember( "shadow_far_dist" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsUint() )
            light->setShadowFarDistance( decodeFloat( tmpIt->value ) );

        tmpIt = lightValue.FindMember( "shadow_clip_dist" );
        if( tmpIt != lightValue.MemberEnd() && tmpIt->value.IsUint() )
        {
            const Vector2 nearFar = decodeVector2Array( tmpIt->value );
            light->setShadowNearClipDistance( nearFar.x );
            light->setShadowFarClipDistance( nearFar.y );
        }

        if( light->getType() == Light::LT_VPL )
            mVplLights.push_back( light );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importLights( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstValueIterator itor = json.Begin();
        rapidjson::Value::ConstValueIterator end  = json.End();

        while( itor != end )
        {
            if( itor->IsObject() )
                importLight( *itor );

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importInstantRadiosity( const rapidjson::Value &json )
    {
        mInstantRadiosity = new InstantRadiosity( mSceneManager, mRoot->getHlmsManager() );

        rapidjson::Value::ConstMemberIterator tmpIt;
        tmpIt = json.FindMember( "first_rq" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mFirstRq = static_cast<uint8>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "last_rq" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mLastRq = static_cast<uint8>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "visibility_mask" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVisibilityMask = static_cast<uint32>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "light_mask" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mLightMask = static_cast<uint32>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "num_rays" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mNumRays = static_cast<size_t>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "num_ray_bounces" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mNumRayBounces = static_cast<size_t>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "surviving_ray_fraction" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mSurvivingRayFraction = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "cell_size" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mCellSize = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "bias" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mBias = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "num_spread_iterations" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mNumSpreadIterations = static_cast<uint32>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "spread_threshold" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mSpreadThreshold = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "areas_of_interest" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsArray() )
        {
            const size_t numAoIs = tmpIt->value.Size();

            for( size_t i=0; i<numAoIs; ++i )
            {
                const rapidjson::Value &aoi = tmpIt->value[i];

                if( aoi.IsArray() && aoi.Size() == 2u &&
                    aoi[0].IsArray() &&
                    aoi[1].IsUint() )
                {
                    const Aabb aabb = decodeAabbArray( aoi[0], Aabb::BOX_ZERO );
                    const float sphereRadius = decodeFloat( aoi[1] );
                    InstantRadiosity::AreaOfInterest areaOfInterest( aabb, sphereRadius );
                    mInstantRadiosity->mAoI.push_back( areaOfInterest );
                }
            }
        }

        tmpIt = json.FindMember( "vpl_max_range" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplMaxRange = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_const_atten" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplConstAtten = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_linear_atten" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplLinearAtten = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_quad_atten" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplQuadAtten = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_threshold" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplThreshold = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_power_boost" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mVplPowerBoost = decodeFloat( tmpIt->value );

        tmpIt = json.FindMember( "vpl_use_intensity_for_max_range" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsBool() )
            mInstantRadiosity->mVplUseIntensityForMaxRange = tmpIt->value.GetBool();

        tmpIt = json.FindMember( "vpl_intensity_range_multiplier" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint64() )
            mInstantRadiosity->mVplIntensityRangeMultiplier = decodeDouble( tmpIt->value );

        tmpIt = json.FindMember( "mipmap_bias" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
            mInstantRadiosity->mMipmapBias = static_cast<uint32>( tmpIt->value.GetUint() );

        tmpIt = json.FindMember( "use_textures" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsBool() )
            mInstantRadiosity->setUseTextures( tmpIt->value.GetBool() );

        tmpIt = json.FindMember( "use_irradiance_volume" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsBool() )
            mInstantRadiosity->setUseIrradianceVolume( tmpIt->value.GetBool() );

        tmpIt = json.FindMember( "irradiance_volume" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsObject() )
        {
            mIrradianceVolume = new IrradianceVolume( mRoot->getHlmsManager() );

            tmpIt = json.FindMember( "num_blocks" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsArray() &&
                tmpIt->value.Size() == 3u &&
                tmpIt->value[0].IsUint() &&
                tmpIt->value[1].IsUint() &&
                tmpIt->value[2].IsUint() )
            {
                mIrradianceVolume->createIrradianceVolumeTexture(
                            tmpIt->value[0].GetUint(),
                            tmpIt->value[1].GetUint(),
                            tmpIt->value[2].GetUint() );
            }

            tmpIt = json.FindMember( "power_scale" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
                mIrradianceVolume->setPowerScale( decodeFloat( tmpIt->value ) );

            tmpIt = json.FindMember( "fade_attenuation_over_distance" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsBool() )
                mIrradianceVolume->setFadeAttenuationOverDistace( tmpIt->value.GetBool() );

            tmpIt = json.FindMember( "irradiance_max_power" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsUint() )
                mIrradianceVolume->setIrradianceMaxPower( decodeFloat( tmpIt->value ) );

            tmpIt = json.FindMember( "irradiance_origin" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsArray() )
                mIrradianceVolume->setIrradianceOrigin( decodeVector3Array( tmpIt->value ) );

            tmpIt = json.FindMember( "irradiance_cell_size" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsArray() )
                mIrradianceVolume->setIrradianceCellSize( decodeVector3Array( tmpIt->value ) );
        }
        else
        {
            mInstantRadiosity->setUseIrradianceVolume( false );
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSceneSettings( const rapidjson::Value &json, uint32 importFlags )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;
        tmpIt = json.FindMember( "ambient" );
        if( tmpIt != json.MemberEnd() && tmpIt->value.IsArray() && tmpIt->value.Size() >= 4u &&
            tmpIt->value[0].IsArray() &&
            tmpIt->value[1].IsArray() &&
            tmpIt->value[2].IsArray() &&
            tmpIt->value[3].IsUint() )
        {
            const ColourValue upperHemisphere = decodeColourValueArray( tmpIt->value[0] );
            const ColourValue lowerHemisphere = decodeColourValueArray( tmpIt->value[1] );
            const Vector3 hemiDir = decodeVector3Array( tmpIt->value[2] );
            const float envmapScale = decodeFloat( tmpIt->value[3] );
            mSceneManager->setAmbientLight( upperHemisphere, lowerHemisphere, hemiDir, envmapScale );
        }

        if( importFlags & SceneFlags::InstantRadiosity )
        {
            tmpIt = json.FindMember( "instant_radiosity" );
            if( tmpIt != json.MemberEnd() && tmpIt->value.IsObject() )
                importInstantRadiosity( tmpIt->value );
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importScene( const String &filename, const rapidjson::Document &d,
                                           uint32 importFlags )
    {
        mFilename = filename;
        destroyInstantRadiosity();

        rapidjson::Value::ConstMemberIterator itor;

        if( importFlags & SceneFlags::SceneNodes )
        {
            itor = d.FindMember( "scene_nodes" );
            if( itor != d.MemberEnd() && itor->value.IsArray() )
                importSceneNodes( itor->value );
        }

        if( importFlags & SceneFlags::Items )
        {
            itor = d.FindMember( "items" );
            if( itor != d.MemberEnd() && itor->value.IsArray() )
                importItems( itor->value );
        }

        if( importFlags & SceneFlags::Entities )
        {
            itor = d.FindMember( "entities" );
            if( itor != d.MemberEnd() && itor->value.IsArray() )
                importEntities( itor->value );
        }

        if( importFlags & SceneFlags::Lights )
        {
            itor = d.FindMember( "lights" );
            if( itor != d.MemberEnd() && itor->value.IsArray() )
                importLights( itor->value );
        }

        itor = d.FindMember( "scene" );
        if( itor != d.MemberEnd() && itor->value.IsObject() )
            importSceneSettings( itor->value, importFlags );

        if( !(importFlags & SceneFlags::LightsVpl) )
        {
            LightArray::const_iterator itLight = mVplLights.begin();
            LightArray::const_iterator enLight = mVplLights.end();

            while( itLight != enLight )
            {
                Light *vplLight = *itLight;
                SceneNode *sceneNode = vplLight->getParentSceneNode();
                mSceneManager->destroySceneNode( sceneNode );
                mSceneManager->destroyLight( vplLight );
                ++itLight;
            }

            mVplLights.clear();
        }

        if( mInstantRadiosity && importFlags & SceneFlags::BuildInstantRadiosity )
        {
            mInstantRadiosity->build();

            HlmsPbs *hlmsPbs = getPbs();
            if( hlmsPbs && mInstantRadiosity->getUseIrradianceVolume() )
                hlmsPbs->setIrradianceVolume( mIrradianceVolume );

            if( mIrradianceVolume )
            {
                mInstantRadiosity->fillIrradianceVolume(
                            mIrradianceVolume,
                            mIrradianceVolume->getIrradianceCellSize(),
                            mIrradianceVolume->getIrradianceOrigin(),
                            mIrradianceVolume->getIrradianceMaxPower(),
                            mIrradianceVolume->getFadeAttenuationOverDistace() );
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importScene( const String &filename, const char *jsonString,
                                           uint32 importFlags )
    {
        rapidjson::Document d;
        d.Parse( jsonString );

        if( d.HasParseError() )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "SceneFormatImporter::importScene",
                         "Invalid JSON string in file " + filename );
        }

        importScene( filename, d, importFlags );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSceneFromFile( const String &folderPath, uint32 importFlags )
    {
        ResourceGroupManager &resourceGroupManager = ResourceGroupManager::getSingleton();
        resourceGroupManager.addResourceLocation( folderPath, "FileSystem", "SceneFormatImporter" );
        resourceGroupManager.addResourceLocation( folderPath + "/v2",
                                                  "FileSystem", "SceneFormatImporter" );
        resourceGroupManager.addResourceLocation( folderPath + "/v1",
                                                  "FileSystem", "SceneFormatImporter" );

        DataStreamPtr stream = resourceGroupManager.openResource( "scene.json", "SceneFormatImporter" );
        vector<char>::type fileData;
        fileData.resize( stream->size() + 1 );
        if( !fileData.empty() )
        {
            stream->read( &fileData[0], stream->size() );

            //Add null terminator just in case (to prevent bad input)
            fileData.back() = '\0';

            rapidjson::Document d;
            d.Parse( &fileData[0] );

            if( d.HasParseError() )
            {
                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                             "SceneFormatImporter::importScene",
                             "Invalid JSON string in file " + stream->getName() );
            }

            rapidjson::Value::ConstMemberIterator  itor;

            bool useOitd = false;
            itor = d.FindMember( "saved_oitd_textures" );
            if( itor != d.MemberEnd() && itor->value.IsBool() )
                useOitd = itor->value.GetBool();

            HlmsManager *hlmsManager = mRoot->getHlmsManager();
            if( useOitd )
                hlmsManager->mAdditionalTextureExtensionsPerGroup["SceneFormatImporter"] = ".oitd";
            resourceGroupManager.initialiseResourceGroup( "SceneFormatImporter", true );
            if( useOitd )
                hlmsManager->mAdditionalTextureExtensionsPerGroup.erase( "SceneFormatImporter" );

            resourceGroupManager.removeResourceLocation( folderPath, "SceneFormatImporter" );
            resourceGroupManager.removeResourceLocation( folderPath + "/v2", "SceneFormatImporter" );
            resourceGroupManager.removeResourceLocation( folderPath + "/v1", "SceneFormatImporter" );

            importScene( stream->getName(), &fileData[0], importFlags );
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::getInstantRadiosity( bool releaseOwnership,
                                                   InstantRadiosity **outInstantRadiosity,
                                                   IrradianceVolume **outIrradianceVolume )
    {
        *outInstantRadiosity = mInstantRadiosity;
        *outIrradianceVolume = mIrradianceVolume;
        if( releaseOwnership )
        {
            mInstantRadiosity = 0;
            mIrradianceVolume = 0;
        }
    }
}