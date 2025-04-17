-- premake5.lua

-- ill come back to this when I need to, this is impossible to use in this ide
workspace "game_engine"
    architecture "x64"
    configurations { "Debug", "Release", "Dist" }
    location "build"   -- Optional: where project files are generated

-- Define an output directory pattern for binaries and intermediates.
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

----------------------------------------------------------------
-- Engine Project (Shared Library)
----------------------------------------------------------------
project "Engine"
    location "engine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin_int/" .. outputdir .. "/%{prj.name}")

    -- Files from the engine source. Because the engine folder is at "engine/"
    -- and your source files live in "engine/src/Honey/", we explicitly list that path.
    files {
        "engine/src/Honey/**.h",
        "engine/src/Honey/**.cpp"
    }

    -- Public include directories for Engine.
    includedirs {
        "engine",                      -- This allows including headers from engine/ (the project root)
        "engine/vendor/spdlog/include"   -- spdlog is header-only.
    }

    defines { "HN_PLATFORM_WINDOWS", "HN_BUILD_DLL" }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        defines { "HN_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "HN_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "HN_DIST" }
        optimize "On"

    filter { "system:windows", "configurations:Release" }
        buildoptions "/MT"

    -- Post-build command: copy the built DLL into the Application binary folder.
    postbuildcommands {
        '{COPY} "%{cfg.buildtarget.abspath}" "../bin/' .. outputdir .. '/Application/"'
    }

----------------------------------------------------------------
-- Application Project (Executable)
----------------------------------------------------------------
project "Application"
    location "application"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin_int/" .. outputdir .. "/%{prj.name}")

    files {
        "application/src/**.h",
        "application/src/**.cpp"
    }

    -- Include engine headers (for using Engine’s public interface) and spdlog headers.
    includedirs {
        "engine/src",                  -- Assuming Engine public headers are found here.
        "engine/vendor/spdlog/include"
    }

    -- Link against the Engine DLL.
    links { "Engine" }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        defines { "HN_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "HN_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "HN_DIST" }
        optimize "On"

    filter { "system:windows", "configurations:Release" }
        buildoptions "/MT"

----------------------------------------------------------------
-- Custom Action: Build and Run the Application
----------------------------------------------------------------
-- This action builds the "Application" target (using the default configuration, here "Debug")
-- and then runs the resulting executable.
--
-- To use this action, first generate your build files (e.g. with gmake2) and then run:
--     premake5 run
--
newaction {
    trigger     = "run",
    description = "Build and run the Application project",
    execute = function ()
        local defaultConfig = "Debug"
        local systemName = os.target()    -- e.g. "windows"
        local arch = "x64"
        local outDir = defaultConfig .. "-" .. systemName .. "-" .. arch
        local exeName = "Application" .. (systemName == "windows" and ".exe" or "")
        local exePath = path.join("bin", outDir, "Application", exeName)

        print("Building the Application project in " .. defaultConfig .. " configuration...")
        local ret = os.execute("make Application")
        if ret ~= 0 then
            error("Build failed!")
        end

        print("Running " .. exePath .. " ...")
        os.execute('"' .. exePath .. '"')
    end
}
