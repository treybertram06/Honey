
-- premake5.lua
workspace "game_engine"
   configurations { "Debug", "Release", "Dist" }
   platforms { "x64" }
   location "build"                -- Generated project files go in the build/ directory.
   startproject "application"      -- Makes application the default startup project.

   filter "configurations:Debug"
      symbols "On"
   filter "configurations:Release"
      optimize "On"
   filter {}  -- clear filter

-------------------------------
-- Engine Project (SharedLib)
-------------------------------
project "engine"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   -- Set output directories for target and intermediate files.
   targetdir ("bin/engine/%{cfg.buildcfg}")
   objdir    ("bin-int/engine/%{cfg.buildcfg}")

   -- List of engine files. (Adjust file patterns if needed.)
   files {
      "engine/src/Honey/engine.cpp",
      "engine/src/Honey/engine.h",
      "engine/src/Honey/core.h",
      "engine/src/Honey/entry_point.h",
      "engine/src/Honey/log.h",
      "engine/src/Honey/log.cpp"
   }

   -- Make the engine directory (the engine root) public so that its headers are available.
   includedirs {
      "engine",
      "engine/vendor/spdlog/include"  -- spdlog header-only dependency.
   }

   -- Define any compile definitions (for example, to handle DLL export/import on Windows).
   defines { "HN_PLATFORM_WINDOWS", "HN_BUILD_DLL" }

   -- Post-build command: copy the engine DLL to the application's target directory.
   postbuildcommands {
      '{COPY} "%{cfg.buildtarget.abspath}" "%{wks.location}/bin/application/%{cfg.buildcfg}"'
   }

-------------------------------
-- Application Project
-------------------------------
project "application"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   targetdir ("bin/application/%{cfg.buildcfg}")
   objdir    ("bin-int/application/%{cfg.buildcfg}")

   -- Gather all source files in the application folder.
   files { "application/src/**.cpp", "application/src/**.h" }

   -- Include directories.
   -- Include the engine's source as well as the vendor directory (if it holds additional headers)
   -- and the spdlog include directory.
   includedirs {
      "engine/src",
      "vendor",
      "engine/vendor/spdlog/include"
   }

   -- Link against the engine shared library.
   links { "engine" }

   -- (Optional) If your C++ code directly uses spdlog from the application,
   -- adding the spdlog include directory is sufficient because it’s header-only.