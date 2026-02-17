#pragma once
#include <JuceHeader.h>

namespace AppPaths
{
    inline bool looksLikeDevRoot(const juce::File& dir)
    {
        if (!dir.isDirectory())
            return false;

        return dir.getChildFile("interface").isDirectory()
            && dir.getChildFile("src").isDirectory()
            && dir.getChildFile("ml").isDirectory()
            && dir.getChildFile("data_pilot").isDirectory()
            && dir.getChildFile(".venv").isDirectory();
    }

    inline juce::File findDevRootFrom(const juce::File& start)
    {
        juce::File dir = start.isDirectory() ? start : start.getParentDirectory();

        while (dir.isDirectory())
        {
            if (looksLikeDevRoot(dir))
                return dir;

            if (dir.isRoot())
                break;

            auto parent = dir.getParentDirectory();
            if (parent == dir)
                break;

            dir = parent;
        }

        return {};
    }

    inline bool looksLikePackagedRoot(const juce::File& dir)
    {
        if (!dir.isDirectory())
            return false;

        if (!dir.getChildFile("ml").isDirectory()
            || !dir.getChildFile("src").isDirectory()
            || !dir.getChildFile("data_pilot").isDirectory())
            return false;

        auto pyRoot = dir.getChildFile("python");
        if (!pyRoot.isDirectory())
            return false;

#if JUCE_WINDOWS
        return pyRoot.getChildFile("python.exe").existsAsFile();
#else
        auto py3 = pyRoot.getChildFile("bin").getChildFile("python3");
        if (py3.existsAsFile())
            return true;

        auto py = pyRoot.getChildFile("bin").getChildFile("python");
        return py.existsAsFile();
#endif
    }

    inline juce::File packagedRootFromExecutable()
    {
        auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

#if JUCE_MAC
        // MyApp.app/Contents/MacOS/MyApp -> Contents/Resources
        auto resources = exe.getParentDirectory()
            .getParentDirectory()
            .getChildFile("Resources");
        if (resources.isDirectory())
            return resources;
#endif

        return exe.getParentDirectory();
    }

    inline juce::File root()
    {
        static juce::File cachedRoot = []()
        {
            // 1) Prefer packaged layout when the executable sits in the distribution root.
            auto packagedRoot = packagedRootFromExecutable();
            if (looksLikePackagedRoot(packagedRoot))
                return packagedRoot;

            // 2) Prefer repository root when running in development.
            auto fromExe = findDevRootFrom(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
            if (fromExe.isDirectory())
                return fromExe;

            auto fromCwd = findDevRootFrom(juce::File::getCurrentWorkingDirectory());
            if (fromCwd.isDirectory())
                return fromCwd;

            auto fromSource = findDevRootFrom(juce::File(juce::String(__FILE__)));
            if (fromSource.isDirectory())
                return fromSource;

            // 3) Fall back to executable parent even if structure is partial.
            return packagedRoot;
        }();

        return cachedRoot;
    }

    inline juce::File dataPilotDir() { return root().getChildFile("data_pilot"); }
    inline juce::File singerUserDir() { return dataPilotDir().getChildFile("singer_user"); }
    inline juce::File workingDataDir() { return singerUserDir(); }
    inline juce::File srcDir() { return root().getChildFile("src"); }
    inline juce::File mlDir() { return root().getChildFile("ml"); }
    inline juce::File configsDir() { return root().getChildFile("configs"); }
    inline juce::File pythonDir()
    {
        auto venv = root().getChildFile(".venv");
        if (venv.isDirectory())
            return venv;

        return root().getChildFile("python");
    }

    inline juce::File pythonExe()
    {
        auto venv = root().getChildFile(".venv");
        if (venv.isDirectory())
        {
#if JUCE_WINDOWS
            auto py = venv.getChildFile("Scripts").getChildFile("python.exe");
            if (py.existsAsFile()) return py;
#else
            auto py3 = venv.getChildFile("bin").getChildFile("python3");
            if (py3.existsAsFile()) return py3;

            auto py = venv.getChildFile("bin").getChildFile("python");
            if (py.existsAsFile()) return py;
#endif
        }

        auto bundledPython = root().getChildFile("python");
#if JUCE_WINDOWS
        return bundledPython.getChildFile("python.exe");
#else
        auto py3 = bundledPython.getChildFile("bin").getChildFile("python3");
        if (py3.existsAsFile()) return py3;
        return bundledPython.getChildFile("bin").getChildFile("python");
#endif
    }

    inline void ensureFoldersExist()
    {
        dataPilotDir().createDirectory();
        workingDataDir().createDirectory();
        configsDir().createDirectory();
        srcDir().createDirectory();
        mlDir().createDirectory();

        // Only create legacy packaged python dir; never create a fake .venv.
        if (!root().getChildFile(".venv").isDirectory())
            root().getChildFile("python").createDirectory();
    }

    //==============================================================================
    // Build a cmd.exe command that captures stderr and forces UTF-8 console
    //==============================================================================
    inline juce::String buildPythonCommand(const juce::StringArray& args)
    {
    #if JUCE_WINDOWS
        // Produces:
        //   cmd.exe /C "chcp 65001 >nul & "python.exe" "-u" "script.py" 2>&1"
        //
        // cmd.exe /C strips the outermost pair of quotes, giving:
        //   chcp 65001 >nul & "python.exe" "-u" "script.py" 2>&1
        //
        // chcp 65001 sets the console codepage to UTF-8 so even Windows
        // error messages are readable. The & runs python afterwards.
        // 2>&1 merges stderr into stdout so we capture everything.
        juce::String cmd = "cmd.exe /C \"chcp 65001 >nul & ";

        for (int i = 0; i < args.size(); ++i)
        {
            if (i > 0) cmd += " ";
            cmd += "\"" + args[i] + "\"";
        }

        cmd += " 2>&1\"";
        return cmd;
    #else
        // On macOS / Linux, simple quoting + stderr redirect
        juce::String cmd;
        for (int i = 0; i < args.size(); ++i)
        {
            if (i > 0) cmd += " ";
            cmd += "'" + args[i].replace("'", "'\\''") + "'";
        }
        cmd += " 2>&1";
        return cmd;
    #endif
    }

    //==============================================================================
    // Write a detailed log entry for every Python invocation
    //==============================================================================
    inline void logPythonExecution(const juce::String& label,
                                   const juce::String& cmdLine,
                                   const juce::String& cwd,
                                   const juce::String& output,
                                   juce::uint32 exitCode)
    {
        juce::File logFile = root().getChildFile("python_debug.log");

        juce::String ts = juce::Time::getCurrentTime().toString(true, true, true, true);
        juce::String entry;
        entry << "\n======== " << label << " [" << ts << "] ========\n"
              << "CMD : " << cmdLine << "\n"
              << "CWD : " << cwd << "\n"
              << "EXIT: " << (int)exitCode << "\n"
              << "PYTHONHOME : " << juce::SystemStats::getEnvironmentVariable("PYTHONHOME", "(not set)") << "\n"
              << "PYTHONPATH : " << juce::SystemStats::getEnvironmentVariable("PYTHONPATH", "(not set)") << "\n"
              << "OUTPUT (" << output.length() << " chars):\n"
              << (output.isEmpty() ? "(empty)\n" : output + "\n")
              << "======== END " << label << " ========\n";

        logFile.appendText(entry);
        juce::Logger::writeToLog(entry);
    }
}
