// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stdafx.h"

#include "compiler.h"
#include "vm.h"
#include "vmdata.h"

#include "sdlincludes.h"    // FIXME: this makes SDL not modular, but without it it will miss the SDLMain indirection
#include "sdlinterface.h"

#include "wentropy.h"

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

using namespace lobster;

const char *fileheader = "\xA5\x74\xEF\x19";
const int fileheaderlen = 4;

void Save(const char *bcf, const vector<uchar> &bytecode)
{
    vector<uchar> out;
    WEntropyCoder<true>(bytecode.data(), bytecode.size(), bytecode.size(), out);

    FILE *f = OpenForWriting(bcf, true);
    if (f)
    {
        fwrite(fileheader, fileheaderlen, 1, f);
        auto len = (uint)bytecode.size();
        fwrite(&len, sizeof(uint), 1, f);  // FIXME: not endianness-safe
        fwrite(out.data(), out.size(), 1, f);
        fclose(f);
    }
}

bool Load(const char *bcf, vector<uchar> &bytecode)
{
    size_t bclen = 0;
    uchar *bc = LoadFile(bcf, &bclen);
    if (!bc) return false;

    if (memcmp(fileheader, bc, fileheaderlen)) { free(bc); throw string("bytecode file corrupt: ") + bcf; }
    uint origlen = *(uint *)(bc + fileheaderlen);
    bytecode.clear();
    WEntropyCoder<false>(bc + fileheaderlen + sizeof(uint), bclen - fileheaderlen - sizeof(uint), origlen, bytecode);

    free(bc);

    return VerifyBytecode(bytecode);
}

void Exit(int code)
{
    extern void GraphicsShutDown(); GraphicsShutDown();

    #ifdef __EMSCRIPTEN__
        emscripten_force_exit(code);
    #endif

    exit(code); // Needed at least on iOS to forcibly shut down the wrapper main()
}

void one_frame_callback()
{
    try
    {
        extern bool GraphicsFrameStart(); GraphicsFrameStart();
        assert(g_vm);
        g_vm->OneMoreFrame();
        // If this returns, we didn't hit a gl_frame() again and exited normally.
        Exit(0);
    }
    catch (string &s)
    {
        if (s != "SUSPEND-VM-MAINLOOP")
        {
            // An actual error.
            Output(OUTPUT_ERROR, s.c_str());
            Exit(1);
        }
    }
}

int main(int argc, char* argv[])
{
    #ifdef _WIN32
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif

    Output(OUTPUT_INFO, "Lobster running...");

    InitTime();

    bool wait = false;
    bool from_bundle = 
    #ifdef __IOS__
        true;
    #else
        false;
    #endif

    try
    {
        RegisterBuiltins();

        bool parsedump = false;
        bool disasm = false;
        const char *default_bcf = "default.lbc";
        const char *bcf = nullptr;

        const char *fn = nullptr;
        for (int arg = 1; arg < argc; arg++) if (argv[arg][0] == '-')
        {
            string a = argv[arg];
            if      (a == "-w") { wait = true; }
            else if (a == "-b") { bcf = default_bcf; }
            else if (a == "--parsedump") { parsedump = true; }
            else if (a == "--disasm")    { disasm = true; }
            else if (a == "--verbose")   { min_output_level = OUTPUT_INFO; }
            else if (a == "--debug")     { min_output_level = OUTPUT_DEBUG; }
            else if (a == "--silent")    { min_output_level = OUTPUT_ERROR; }
            else if (a == "--gen-builtins-html")  { DumpBuiltins(false); return 0; }
            else if (a == "--gen-builtins-names") { DumpBuiltins(true);  return 0; }
            else if (a == "--non-interactive-test") { SDLTestMode(); }
            else if (a == "-c") {}  // deprecated, remove this one, not needed anymore.
            else if (a.substr(0, 5) == "-psn_") { from_bundle = true; }  // process identifier supplied by OS X
            else throw string("unknown command line argument: ") + argv[arg];
        }
        else
        {
            if (fn) throw string("more than one file specified");
            fn = argv[arg];
        }

        #ifdef __IOS__
            //fn = "totslike.lobster";  // FIXME: temp solution
        #endif

        if (!SetupDefaultDirs(argv[0], fn, from_bundle))
            throw string("cannot find location to read/write data on this platform!");

        vector<uchar> bytecode;

        if (!fn)
        {
            if (!Load(default_bcf, bytecode))
                throw string("Lobster programming language compiler/runtime (version " __DATE__ 
                             ")\nno arguments given - cannot load ") + default_bcf;
        }
        else
        {
            Output(OUTPUT_INFO, "compiling...");

            string dump;
            Compile(StripDirPart(fn).c_str(), nullptr, bytecode, parsedump ? &dump : nullptr);

            if (parsedump)
            {
                FILE *f = OpenForWriting("parsedump.txt", false);
                if (f)
                {
                    fprintf(f, "%s\n", dump.c_str());
                    fclose(f);
                }
            }

            if (bcf)
            {
                Save(bcf, bytecode);
                return 0;
            }
        }

        if (disasm)
        {
            FILE *f = OpenForWriting("disasm.txt", false);
            if (f)
            {
                string s;
                DisAsm(s, bytecode.data());
                fputs(s.c_str(), f);
                fclose(f);
            }
        }

        #ifdef USE_MAIN_LOOP_CALLBACK
        try
        {
        #endif

        RunBytecode(fn ? StripDirPart(fn).c_str() : "", std::move(bytecode));

        #ifdef USE_MAIN_LOOP_CALLBACK
        }
        catch (string &s)
        {
            if (s == "SUSPEND-VM-MAINLOOP")
            {
                // emscripten requires that we don't control the main loop.
                // We just got to the start of the first frame inside gl_frame(), and the VM is suspended.
                // Install the one-frame callback:
                #ifdef __EMSCRIPTEN__
                emscripten_set_main_loop(one_frame_callback, 0, false);
                // Return from main() here (!) since we don't actually want to run any shutdown code yet.
                assert(g_vm);
                return 0;
                #else
                // Emulate this behavior so we can debug it.
                while (g_vm->evalret == "") one_frame_callback();
                #endif
            }
            else
            {
                // An actual error.
                throw s;
            }
        }
        #endif

        delete g_vm;
    }
    catch (string &s)
    {
        if (g_vm) delete g_vm;
        Output(OUTPUT_ERROR, s.c_str());
        if (from_bundle) MsgBox(s.c_str());
        if (wait)
        {
            Output(OUTPUT_PROGRAM, "press <ENTER> to continue:\n");
            getchar();
        }

        #ifdef _WIN32
            _CrtSetDbgFlag(0);  // Don't bother with memory leaks when there was an error.
        #endif

        Exit(1);
    }

    Exit(0);
    return 0;
}

