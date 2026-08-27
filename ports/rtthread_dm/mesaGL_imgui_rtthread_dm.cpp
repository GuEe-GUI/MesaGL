#include "mesaGL_imgui_rtthread_dm.h"

#include "imgui.h"

#include <dt-bindings/input/event-codes.h>

static ImGuiKey translate_key(unsigned code)
{
    if (code >= KEY_1 && code <= KEY_9)
        return (ImGuiKey)(ImGuiKey_1 + code - KEY_1);
    if (code >= KEY_F1 && code <= KEY_F12)
        return (ImGuiKey)(ImGuiKey_F1 + code - KEY_F1);
    switch (code) {
    case KEY_0: return ImGuiKey_0;
    case KEY_A: return ImGuiKey_A;
    case KEY_B: return ImGuiKey_B;
    case KEY_C: return ImGuiKey_C;
    case KEY_D: return ImGuiKey_D;
    case KEY_E: return ImGuiKey_E;
    case KEY_F: return ImGuiKey_F;
    case KEY_G: return ImGuiKey_G;
    case KEY_H: return ImGuiKey_H;
    case KEY_I: return ImGuiKey_I;
    case KEY_J: return ImGuiKey_J;
    case KEY_K: return ImGuiKey_K;
    case KEY_L: return ImGuiKey_L;
    case KEY_M: return ImGuiKey_M;
    case KEY_N: return ImGuiKey_N;
    case KEY_O: return ImGuiKey_O;
    case KEY_P: return ImGuiKey_P;
    case KEY_Q: return ImGuiKey_Q;
    case KEY_R: return ImGuiKey_R;
    case KEY_S: return ImGuiKey_S;
    case KEY_T: return ImGuiKey_T;
    case KEY_U: return ImGuiKey_U;
    case KEY_V: return ImGuiKey_V;
    case KEY_W: return ImGuiKey_W;
    case KEY_X: return ImGuiKey_X;
    case KEY_Y: return ImGuiKey_Y;
    case KEY_Z: return ImGuiKey_Z;
    case KEY_TAB: return ImGuiKey_Tab;
    case KEY_LEFT: return ImGuiKey_LeftArrow;
    case KEY_RIGHT: return ImGuiKey_RightArrow;
    case KEY_UP: return ImGuiKey_UpArrow;
    case KEY_DOWN: return ImGuiKey_DownArrow;
    case KEY_PAGEUP: return ImGuiKey_PageUp;
    case KEY_PAGEDOWN: return ImGuiKey_PageDown;
    case KEY_HOME: return ImGuiKey_Home;
    case KEY_END: return ImGuiKey_End;
    case KEY_INSERT: return ImGuiKey_Insert;
    case KEY_DELETE: return ImGuiKey_Delete;
    case KEY_BACKSPACE: return ImGuiKey_Backspace;
    case KEY_SPACE: return ImGuiKey_Space;
    case KEY_ENTER: return ImGuiKey_Enter;
    case KEY_ESC: return ImGuiKey_Escape;
    case KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case KEY_COMMA: return ImGuiKey_Comma;
    case KEY_MINUS: return ImGuiKey_Minus;
    case KEY_DOT: return ImGuiKey_Period;
    case KEY_SLASH: return ImGuiKey_Slash;
    case KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case KEY_EQUAL: return ImGuiKey_Equal;
    case KEY_LEFTBRACE: return ImGuiKey_LeftBracket;
    case KEY_BACKSLASH: return ImGuiKey_Backslash;
    case KEY_RIGHTBRACE: return ImGuiKey_RightBracket;
    case KEY_GRAVE: return ImGuiKey_GraveAccent;
    case KEY_CAPSLOCK: return ImGuiKey_CapsLock;
    case KEY_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case KEY_NUMLOCK: return ImGuiKey_NumLock;
    case KEY_SYSRQ: return ImGuiKey_PrintScreen;
    case KEY_PAUSE: return ImGuiKey_Pause;
    case KEY_LEFTSHIFT: return ImGuiKey_LeftShift;
    case KEY_RIGHTSHIFT: return ImGuiKey_RightShift;
    case KEY_LEFTCTRL: return ImGuiKey_LeftCtrl;
    case KEY_RIGHTCTRL: return ImGuiKey_RightCtrl;
    case KEY_LEFTALT: return ImGuiKey_LeftAlt;
    case KEY_RIGHTALT: return ImGuiKey_RightAlt;
    case KEY_LEFTMETA: return ImGuiKey_LeftSuper;
    case KEY_RIGHTMETA: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

static unsigned key_character(ImGuiKey key, bool shift)
{
    static const char shifted_digits[] = ")!@#$%^&*(";

    if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
        return (unsigned)(shift ? 'A' : 'a') + key - ImGuiKey_A;
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
        return shift ? shifted_digits[key - ImGuiKey_0]
                     : (unsigned)'0' + key - ImGuiKey_0;
    switch (key) {
    case ImGuiKey_Space: return ' ';
    case ImGuiKey_Apostrophe: return shift ? '"' : '\'';
    case ImGuiKey_Comma: return shift ? '<' : ',';
    case ImGuiKey_Minus: return shift ? '_' : '-';
    case ImGuiKey_Period: return shift ? '>' : '.';
    case ImGuiKey_Slash: return shift ? '?' : '/';
    case ImGuiKey_Semicolon: return shift ? ':' : ';';
    case ImGuiKey_Equal: return shift ? '+' : '=';
    case ImGuiKey_LeftBracket: return shift ? '{' : '[';
    case ImGuiKey_Backslash: return shift ? '|' : '\\';
    case ImGuiKey_RightBracket: return shift ? '}' : ']';
    case ImGuiKey_GraveAccent: return shift ? '~' : '`';
    default: return 0;
    }
}

static void process_event(void *, const MesaGLRTThreadDMEvent *event)
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

    if (event->type == MESAGL_RTTHREAD_DM_POINTER) {
        io.AddMousePosEvent((float)event->x, (float)event->y);
    } else if (event->type == MESAGL_RTTHREAD_DM_BUTTON) {
        int button = event->code == BTN_RIGHT    ? 1
                     : event->code == BTN_MIDDLE ? 2
                                                  : 0;

        io.AddMousePosEvent((float)event->x, (float)event->y);
        io.AddMouseButtonEvent(button, event->down != 0);
    } else if (event->type == MESAGL_RTTHREAD_DM_KEY) {
        ImGuiKey key = translate_key(event->code);
        bool down = event->down != 0;

        if (key == ImGuiKey_None)
            return;
        io.AddKeyEvent(key, down);
        io.SetKeyEventNativeData(key, (int)event->code, (int)event->code);
        if (key == ImGuiKey_LeftCtrl)
            left_ctrl = down;
        else if (key == ImGuiKey_RightCtrl)
            right_ctrl = down;
        else if (key == ImGuiKey_LeftShift)
            left_shift = down;
        else if (key == ImGuiKey_RightShift)
            right_shift = down;
        else if (key == ImGuiKey_LeftAlt)
            left_alt = down;
        else if (key == ImGuiKey_RightAlt)
            right_alt = down;
        else if (key == ImGuiKey_LeftSuper)
            left_super = down;
        else if (key == ImGuiKey_RightSuper)
            right_super = down;
        io.AddKeyEvent(ImGuiMod_Ctrl, left_ctrl || right_ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, left_shift || right_shift);
        io.AddKeyEvent(ImGuiMod_Alt, left_alt || right_alt);
        io.AddKeyEvent(ImGuiMod_Super, left_super || right_super);
        if (down && !left_ctrl && !right_ctrl && !left_alt && !right_alt) {
            unsigned character = key_character(
                key, left_shift || right_shift);

            if (character)
                io.AddInputCharacter(character);
        }
    }
}

void mesaGLImGuiRTThreadDMInit(MesaGLRTThreadDM *port)
{
    ImGui::GetIO().BackendPlatformName = "mesaGL_rtthread_dm";
    mesaGLRTThreadDMSetEventCallback(port, process_event, NULL);
}

void mesaGLImGuiRTThreadDMShutdown(MesaGLRTThreadDM *port)
{
    mesaGLRTThreadDMSetEventCallback(port, NULL, NULL);
    ImGui::GetIO().BackendPlatformName = NULL;
}
