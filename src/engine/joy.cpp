#include"engine.h"
namespace joystick
{
    bool isenabled = false;
    int activeindex = -1;
    SDL_Joystick *stick = NULL;

    void acquire(int joystickindex)
    {
        if (!isenabled)
        {
            SDL_InitSubSystem(SDL_INIT_JOYSTICK);
            isenabled = true;
        }
        int count = SDL_NumJoysticks();
        if (count > joystickindex)
        {
            stick = SDL_JoystickOpen(joystickindex);
            if (!stick)
            {
                conoutf(CON_ERROR, "Failed to open joystick %d", joystickindex);
            }
            else
            {
                activeindex = joystickindex;
                conoutf
                    ( CON_DEBUG
                    , "Opened joystick \"%s\" with %d axes and %d buttons"
                    , SDL_JoystickName(stick)
                    , SDL_JoystickNumAxes(stick)
                    , SDL_JoystickNumButtons(stick)
                    );
            }
        }
        else
        {
            conoutf
                ( CON_ERROR
                , "Joystick is enabled and index %d selected, but there are only %d joysticks"
                , joystickindex
                , count
                );
        }
    }

    void release()
    {
        if (stick)
        {
            if (SDL_JoystickGetAttached(stick))
                SDL_JoystickClose(stick);
            stick = NULL;
            activeindex = -1;
        }
        if (isenabled)
        {
            isenabled = false;
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        }
    }

    void setenabled(int joystickindex)
    {
        if (joystickindex == activeindex) return;
        if (joystickindex >= 0)
        {
            acquire(joystickindex);
        }
        else
        {
            release();
        }
    }

    void handleaxis(const SDL_JoyAxisEvent &e)
    {
        conoutf("Axis %d value %d", e.axis, e.value);
    }

    void handleevent(const SDL_Event &e)
    {
        switch (e.type)
        {
        case SDL_JOYAXISMOTION:
            handleaxis(e.jaxis);
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            break;
        }
    }

    VARFP(joystick, -1, -1, 100, { setenabled(joystick); });
}
