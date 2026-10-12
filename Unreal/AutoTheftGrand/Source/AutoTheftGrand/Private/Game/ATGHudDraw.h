// hud.js's drawing helpers that more than the HUD uses (the pause menu's map): drawBlip and drawPlayerArrow.
#pragma once

#include "CoreMinimal.h"

class FATGPainter;
namespace atg { struct Blip; }

namespace ATGHudDraw {
// drawPlayerArrow: Radar true for the radar (RelYaw 0 points up), false for the map (world yaw, +z down)
void PlayerArrow(FATGPainter& C, float X, float Y, float Yaw, float Scale, bool bRadar);
// drawBlip: a letter disc, an icon on a dark tile, or a dot / square
void Blip(FATGPainter& C, float X, float Y, const atg::Blip& B, float Scale);
}
