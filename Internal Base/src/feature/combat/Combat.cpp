#include "Combat.h"
#include "legitbot/Aimbot.h"
#include "legitbot/Triggerbot.h"
#include "legitbot/RCS.h"

void Combat::Run()
{
    Aimbot::Run();
    Triggerbot::Run();
    RCS::Run();
}

void Combat::Render()
{
    Aimbot::DrawFov();
    Triggerbot::DrawFov();
}
