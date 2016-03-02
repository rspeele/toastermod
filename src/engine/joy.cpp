#include"engine.h"
namespace joystick
{
    bool isenabled = false;
    int activeindex = -1;
    SDL_Joystick *stick = NULL;

    enum
    {
        AXIS_X = 0, // left/right lean
        AXIS_Y, // forward/back lean
        AXIS_Z // throttle
    };
    const float axismax = 32767.5f;
    const int buttonsym = -100;
    const int hatsym = -200;

    void acquire(const int joystickindex)
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

    void setenabled(const int joystickindex)
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

    float axis(const int value)
    {
        const float scaled = -value / axismax;
        return scaled;
    }

    void handleaxis(const SDL_JoyAxisEvent &e)
    {
        switch (e.axis)
        {
        case AXIS_X:
            player->fstrafe = clamp(axis(e.value), -1.0f, 1.0f);
            break;
        case AXIS_Y:
            player->fmove = clamp(axis(e.value), -1.0f, 1.0f);
            break;
        }
    }

    void handlebutton(const SDL_JoyButtonEvent &e)
    {
        int symbol = buttonsym - e.button;
        processkey(symbol, e.state == SDL_PRESSED);
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
            handlebutton(e.jbutton);
            break;
        }
    }

    VARFP(joystick, -1, -1, 100, { setenabled(joystick); });
}
