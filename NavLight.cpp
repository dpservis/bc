/*   Bridge Command 5.0 Ship Simulator
     Copyright (C) 2014 James Packer

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY Or FITNESS For A PARTICULAR PURPOSE.  See the
     GNU General Public License For more details.

     You should have received a copy of the GNU General Public License along
     with this program; if not, write to the Free Software Foundation, Inc.,
     51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "NavLight.hpp"
#include "Angles.hpp"

#include <iostream>
#include <cmath> //For fmod()
#include <cstdlib> //For rand()

using namespace irr;

NavLightCallback::NavLightCallback()
{
    firstRun = true;
    lightLevel = 0;
}

void NavLightCallback::OnSetConstants(video::IMaterialRendererServices* services, s32 userData)
{
    if (firstRun) {
        firstRun = false;
        services->setPixelShaderConstant(services->getVertexShaderConstantID("lightLevel"), &lightLevel, 1);
        //services->setPixelShaderConstant("lightLevel", &lightLevel, 1);
    }
}

void NavLightCallback::setLightLevel(irr::f32 lightLevel)
{
    this->lightLevel = lightLevel;
    //std::cout << lightLevel << std::endl;
}

NavLight::NavLight(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* smgr, irr::core::dimension2d<irr::f32> lightSize, irr::core::vector3df position, irr::video::SColor colour, irr::f32 lightStartAngle, irr::f32 lightEndAngle, irr::f32 lightRange, std::string lightSequence, irr::u32 phaseStart) {

    //Store the scene manager, so we can find the active camera
    this->smgr = smgr;

    shaderCallback = new NavLightCallback; //Todo - drop or delete as required

    lightNode = smgr->addBillboardSceneNode(parent, lightSize, position);

    //TODO: Implement for directX as well
    irr::s32 shader;
    if (smgr->getVideoDriver()->getDriverType() == irr::video::EDT_OPENGL) {
    shader = smgr->getVideoDriver()->getGPUProgrammingServices()->addHighLevelShaderMaterialFromFiles(
            "shaders/NavLight_vs.glsl",
            "main",
            irr::video::EVST_VS_2_0,
            "shaders/NavLight_ps.glsl",
            "main",
            irr::video::EPST_PS_2_0,
            shaderCallback, //For callbacks
            irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL
            //irr::video::EMT_SOLID
            );
    }

    shader = shader==-1?0:shader; //Just in case something goes horribly wrong...

    //Apply shader
    lightNode->setColor(colour);
    for (u32 i=0; i<lightNode->getMaterialCount(); ++i)
    {
        lightNode->getMaterial(i).setTexture(0,smgr->getVideoDriver()->getTexture("media/particlewhite.png"));
        lightNode->getMaterial(i).MaterialType = (irr::video::E_MATERIAL_TYPE)shader;
        lightNode->getMaterial(i).FogEnable = true;
    }


    //Fix angles if start is negative
    while (lightStartAngle < 0) {
        lightStartAngle +=360;
        lightEndAngle +=360;
    }

    //store extra information
    startAngle = lightStartAngle;
    endAngle = lightEndAngle;
    range = lightRange;

    //initialise light sequence information
    charTime = 0.25; //where each character represents 0.25s of time
    sequence = lightSequence;
    if (phaseStart==0) {
        timeOffset=60.0*((irr::f32)std::rand()/RAND_MAX); //Random, 0-60s
    } else {
        timeOffset=(phaseStart-1)*charTime;
    }

    //set initial alpha to implausible value
//    currentAlpha = -1;
}

NavLight::~NavLight() {
    //TODO: Understand why NavLights are being created and destroyed during model set-up
}

irr::core::vector3df NavLight::getPosition() const
{
    lightNode->updateAbsolutePosition();//ToDo: This may be needed, but seems odd that it's required
    return lightNode->getAbsolutePosition();
}

void NavLight::setPosition(irr::core::vector3df position)
{
    lightNode->setPosition(position);
}

void NavLight::update(irr::f32 scenarioTime, irr::u32 lightLevel) {

    //FIXME: Remove viewPosition being passed in (now from Camera), and check if camera is null.

    //find light position
    lightNode->updateAbsolutePosition(); //ToDo: This is needed, but seems odd that it's required
    core::vector3df lightPosition = lightNode->getAbsolutePosition();

    //Find the active camera
    irr::scene::ICameraSceneNode* camera = smgr->getActiveCamera();
    if (camera == 0) {
        return; //If we don't know where the camera is, we can't update lights etc, so give up here.
    }
    camera->updateAbsolutePosition();
    irr::core::vector3df viewPosition = camera->getAbsolutePosition();

    //find the HFOV
    irr::f32 hFOV = 2*atan(tan(camera->getFOV()/2)*camera->getAspectRatio()); //Convert from VFOV to hFOV
    irr::f32 zoom = hFOV / (core::PI/2.0); //Zoom compared to standard 90 degree field of view

    //scale so lights appear same size independent of range
    f32 lightDistance=lightPosition.getDistanceFrom(viewPosition);
    lightNode->setSize(core::dimension2df(lightDistance*0.01*zoom,lightDistance*0.01*zoom));

    //set light visibility depending on range
    if (lightDistance > range) {
        lightNode->setVisible(false);
    } else {
        lightNode->setVisible(true);
    }

    //set light visibility depending on angle: Set to false if not visible
    f32 relativeAngleDeg = (viewPosition-lightPosition).getHorizontalAngle().Y; //Degrees: Angle from the light to viewpoint.
    f32 parentAngleDeg = lightNode->getParent()->getRotation().Y;
    f32 localRelativeAngleDeg = relativeAngleDeg-parentAngleDeg; //Angle from light to viewpoint, relative to light's parent coordinate system.
    if (!Angles::isAngleBetween(localRelativeAngleDeg,startAngle,endAngle)) {
        lightNode->setVisible(false);
    }

    //set light visibility depending on light sequence
    //find length of sequence
    std::string::size_type sequenceLength = sequence.length();
    if (sequenceLength > 0) {
        f32 timeInSequence = std::fmod(((scenarioTime+timeOffset) / charTime),sequenceLength);
        u32 positionInSequence = timeInSequence;
        if (positionInSequence>=sequenceLength) {positionInSequence = sequenceLength-1;} //Should not be required, but double check we're not off the end of the sequence
        if (sequence[positionInSequence] == 'D' || sequence[positionInSequence] == 'd') {
            lightNode->setVisible(false);
        }
    }

    shaderCallback->setLightLevel((f32)lightLevel/256); //Convert to float for shader

}

/*
bool NavLight::setAlpha(irr::u8 alpha, irr::video::ITexture* tex)
//Modified from http://irrlicht.sourceforge.net/forum/viewtopic.php?t=31400
//FIXME: Check how the texture color format is set
{

    //return true;

    std::cout << "Setting alpha to " << static_cast<int>(alpha) << std::endl;

    if(!tex) {
        return false;
    }

    u32 size = tex->getSize().Width*tex->getSize().Height;  // get Texture Size

    switch(tex->getColorFormat()) //getTexture Format, (nly 2 support alpha)
    {
        case video::ECF_A1R5G5B5: //see video::ECOLOR_FORMAT for more information on the texture formats.
        {

            std::cout << "video::ECF_A1R5G5B5" << std::endl;
          //  printf("16BIT\n");
            u16* Data = (u16*)tex->lock(); //get Data for 16-bit Texture
            for(u32 i = 0; i < size ; i++)
            {
                u8 alphaToUse = (u8)video::getAlpha(Data[i])==0 ? 0 : alpha; //If already transparent, leave as-is
                Data[i] = video::RGBA16(video::getRed(Data[i]), video::getGreen(Data[i]), video::getBlue(Data[i]), alphaToUse);
            }
            tex->unlock();
            break;
        };
        case video::ECF_A8R8G8B8:
        {
            std::cout << "video::ECF_A8R8G8B8" << std::endl;
            u32* Data = (u32*)tex->lock();
            if (Data) {
                for( u32 i = 0 ; i < size ; i++)
                {

                    //video::SColor currentColor(Data[i]);
                    //u32 alphaToUse = currentColor.getAlpha() == 0 ? 0 : alpha;
                    //Data[i] = video::SColor(alphaToUse, currentColor.getRed(), currentColor.getGreen(), currentColor.getBlue()).color;

                    u8 alphaToUse = ((u8*)&Data[i])[3] == 0 ? 0 : alpha; //If already transparent, leave as-is
                    ((u8*)&Data[i])[3] = alphaToUse;//get Data for 32-bit Texture
                }
            }
            tex->unlock();
            tex->regenerateMipMapLevels();
            break;
        };
        default:
            return false;
    };
    return true;
}
*/

void NavLight::moveNode(irr::f32 deltaX, irr::f32 deltaY, irr::f32 deltaZ)
{
    core::vector3df currentPos = lightNode->getPosition();
    irr::f32 newPosX = currentPos.X + deltaX;
    irr::f32 newPosY = currentPos.Y + deltaY;
    irr::f32 newPosZ = currentPos.Z + deltaZ;

    lightNode->setPosition(core::vector3df(newPosX,newPosY,newPosZ));
}
