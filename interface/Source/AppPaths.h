#pragma once
#include <JuceHeader.h>

namespace AppPaths
{
    // Root = folder where the .exe lives (Windows/Linux).
    // (Optional macOS Resources handling included but harmless elsewhere)
    inline juce::File root()
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

    inline juce::File dataPilotDir() { return root().getChildFile("data_pilot"); }
    inline juce::File singerUserDir() { return dataPilotDir().getChildFile("singer_user"); }
    inline juce::File srcDir() { return root().getChildFile("src"); }
    inline juce::File mlDir() { return root().getChildFile("ml"); }
    inline juce::File configsDir() { return root().getChildFile("configs"); }
    inline juce::File pythonDir() { return root().getChildFile("python"); }

    inline juce::File pythonExe()
    {
#if JUCE_WINDOWS
        return pythonDir().getChildFile("python.exe");
#else
        auto py3 = pythonDir().getChildFile("bin").getChildFile("python3");
        if (py3.existsAsFile()) return py3;
        return pythonDir().getChildFile("bin").getChildFile("python");
#endif
    }

    inline void ensureFoldersExist()
    {
        dataPilotDir().createDirectory();
        singerUserDir().createDirectory();
        configsDir().createDirectory();
        srcDir().createDirectory();
        mlDir().createDirectory();
        pythonDir().createDirectory();
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
