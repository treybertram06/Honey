#ifdef HN_PLATFORM_WINDOWS
#include "hnpch.h"
#include "Honey/utils/platform_utils.h"

#include <commdlg.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "Honey/core/engine.h"

namespace Honey {

    std::string FileDialogs::open_file(const char *filter) {
        OPENFILENAMEA ofn;
        CHAR sz_file[260] = { 0 };

        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = glfwGetWin32Window(static_cast<GLFWwindow*>(Application::get().get_window().get_native_window()));
        ofn.lpstrFile = sz_file;
        ofn.nMaxFile = sizeof(sz_file);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
    }

    std::string FileDialogs::save_file(const char *filter) {
        OPENFILENAMEA ofn;
        CHAR sz_file[260] = { 0 };

        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = glfwGetWin32Window(static_cast<GLFWwindow*>(Application::get().get_window().get_native_window()));
        ofn.lpstrFile = sz_file;
        ofn.nMaxFile = sizeof(sz_file);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
    }

}

#endif