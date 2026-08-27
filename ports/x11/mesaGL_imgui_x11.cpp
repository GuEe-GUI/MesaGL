#include "mesaGL_imgui_x11.h"

#include "imgui.h"

#include <X11/keysym.h>
#include <float.h>

static ImGuiKey translate_key(unsigned long key)
{
    if (key >= XK_0 && key <= XK_9)
        return (ImGuiKey)(ImGuiKey_0 + key - XK_0);
    if (key >= XK_A && key <= XK_Z)
        return (ImGuiKey)(ImGuiKey_A + key - XK_A);
    if (key >= XK_a && key <= XK_z)
        return (ImGuiKey)(ImGuiKey_A + key - XK_a);
    if (key >= XK_F1 && key <= XK_F12)
        return (ImGuiKey)(ImGuiKey_F1 + key - XK_F1);

    switch (key) {
    case XK_Tab: return ImGuiKey_Tab;
    case XK_Left: return ImGuiKey_LeftArrow;
    case XK_Right: return ImGuiKey_RightArrow;
    case XK_Up: return ImGuiKey_UpArrow;
    case XK_Down: return ImGuiKey_DownArrow;
    case XK_Page_Up: return ImGuiKey_PageUp;
    case XK_Page_Down: return ImGuiKey_PageDown;
    case XK_Home: return ImGuiKey_Home;
    case XK_End: return ImGuiKey_End;
    case XK_Insert: return ImGuiKey_Insert;
    case XK_Delete: return ImGuiKey_Delete;
    case XK_BackSpace: return ImGuiKey_Backspace;
    case XK_space: return ImGuiKey_Space;
    case XK_Return: return ImGuiKey_Enter;
    case XK_Escape: return ImGuiKey_Escape;
    case XK_apostrophe: return ImGuiKey_Apostrophe;
    case XK_comma: return ImGuiKey_Comma;
    case XK_minus: return ImGuiKey_Minus;
    case XK_period: return ImGuiKey_Period;
    case XK_slash: return ImGuiKey_Slash;
    case XK_semicolon: return ImGuiKey_Semicolon;
    case XK_equal: return ImGuiKey_Equal;
    case XK_bracketleft: return ImGuiKey_LeftBracket;
    case XK_backslash: return ImGuiKey_Backslash;
    case XK_bracketright: return ImGuiKey_RightBracket;
    case XK_grave: return ImGuiKey_GraveAccent;
    case XK_Caps_Lock: return ImGuiKey_CapsLock;
    case XK_Scroll_Lock: return ImGuiKey_ScrollLock;
    case XK_Num_Lock: return ImGuiKey_NumLock;
    case XK_Print: return ImGuiKey_PrintScreen;
    case XK_Pause: return ImGuiKey_Pause;
    case XK_KP_0: return ImGuiKey_Keypad0;
    case XK_KP_1: return ImGuiKey_Keypad1;
    case XK_KP_2: return ImGuiKey_Keypad2;
    case XK_KP_3: return ImGuiKey_Keypad3;
    case XK_KP_4: return ImGuiKey_Keypad4;
    case XK_KP_5: return ImGuiKey_Keypad5;
    case XK_KP_6: return ImGuiKey_Keypad6;
    case XK_KP_7: return ImGuiKey_Keypad7;
    case XK_KP_8: return ImGuiKey_Keypad8;
    case XK_KP_9: return ImGuiKey_Keypad9;
    case XK_KP_Decimal: return ImGuiKey_KeypadDecimal;
    case XK_KP_Divide: return ImGuiKey_KeypadDivide;
    case XK_KP_Multiply: return ImGuiKey_KeypadMultiply;
    case XK_KP_Subtract: return ImGuiKey_KeypadSubtract;
    case XK_KP_Add: return ImGuiKey_KeypadAdd;
    case XK_KP_Enter: return ImGuiKey_KeypadEnter;
    case XK_KP_Equal: return ImGuiKey_KeypadEqual;
    case XK_Shift_L: return ImGuiKey_LeftShift;
    case XK_Shift_R: return ImGuiKey_RightShift;
    case XK_Control_L: return ImGuiKey_LeftCtrl;
    case XK_Control_R: return ImGuiKey_RightCtrl;
    case XK_Alt_L: return ImGuiKey_LeftAlt;
    case XK_Alt_R: return ImGuiKey_RightAlt;
    case XK_Super_L: return ImGuiKey_LeftSuper;
    case XK_Super_R: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

static void process_event(void *, const MesaGLX11Event *event)
{
    static bool left_ctrl;
    static bool right_ctrl;
    static bool left_shift;
    static bool right_shift;
    static bool left_alt;
    static bool right_alt;
    static bool left_super;
    static bool right_super;
    ImGuiIO &io = ImGui::GetIO();

    switch (event->type) {
    case MESAGL_X11_MOUSE_POSITION:
        io.AddMousePosEvent(event->x < -1000000 ? -FLT_MAX : (float)event->x,
                            event->y < -1000000 ? -FLT_MAX : (float)event->y);
        break;
    case MESAGL_X11_MOUSE_BUTTON:
        io.AddMouseButtonEvent(event->button, event->down != 0);
        break;
    case MESAGL_X11_MOUSE_WHEEL:
        io.AddMouseWheelEvent(event->wheel_x, event->wheel_y);
        break;
    case MESAGL_X11_KEY: {
        ImGuiKey key = translate_key(event->key);

        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, event->down != 0);
            io.SetKeyEventNativeData(key, (int)event->key, (int)event->keycode);
            if (key == ImGuiKey_LeftCtrl)
                left_ctrl = event->down != 0;
            else if (key == ImGuiKey_RightCtrl)
                right_ctrl = event->down != 0;
            else if (key == ImGuiKey_LeftShift)
                left_shift = event->down != 0;
            else if (key == ImGuiKey_RightShift)
                right_shift = event->down != 0;
            else if (key == ImGuiKey_LeftAlt)
                left_alt = event->down != 0;
            else if (key == ImGuiKey_RightAlt)
                right_alt = event->down != 0;
            else if (key == ImGuiKey_LeftSuper)
                left_super = event->down != 0;
            else if (key == ImGuiKey_RightSuper)
                right_super = event->down != 0;
            io.AddKeyEvent(ImGuiMod_Ctrl, left_ctrl || right_ctrl);
            io.AddKeyEvent(ImGuiMod_Shift, left_shift || right_shift);
            io.AddKeyEvent(ImGuiMod_Alt, left_alt || right_alt);
            io.AddKeyEvent(ImGuiMod_Super, left_super || right_super);
        }
        break;
    }
    case MESAGL_X11_TEXT:
        io.AddInputCharactersUTF8(event->text);
        break;
    case MESAGL_X11_FOCUS:
        io.AddFocusEvent(event->down != 0);
        if (!event->down) {
            left_ctrl = right_ctrl = false;
            left_shift = right_shift = false;
            left_alt = right_alt = false;
            left_super = right_super = false;
        }
        break;
    }
}

void mesaGLImGuiX11Init(MesaGLX11 *x11)
{
    ImGui::GetIO().BackendPlatformName = "mesaGL_x11";
    mesaGLX11SetEventCallback(x11, process_event, NULL);
}

void mesaGLImGuiX11Shutdown(MesaGLX11 *x11)
{
    mesaGLX11SetEventCallback(x11, NULL, NULL);
    ImGui::GetIO().BackendPlatformName = NULL;
}
