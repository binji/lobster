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

namespace lobster
{

static const bytecode::LineInfo *LookupLine(const int *ip, const int *code, const bytecode::BytecodeFile *bcf)
{
    auto lineinfo = bcf->lineinfo();
    int pos = int(ip - code);
    int start = 0;
    auto size = lineinfo->size();
    assert(size);

    for (;;)    // quick hardcoded binary search
    {
        if (size == 1) return lineinfo->Get(start);

        auto nsize = size / 2;
        if (lineinfo->Get(start + nsize)->bytecodestart() <= pos) { start += nsize; size -= nsize; }
        else size = nsize;
    }
}

static void LvalDisAsm(string &s, const int *&ip)
{
    #define F(N) #N,
    static const char *lvonames[] = { LVALOPNAMES };
    #undef F

    s += lvonames[*ip++];
    s += " ";
}

static const char *IdName(const bytecode::BytecodeFile *bcf, int i)
{
    return bcf->idents()->Get(bcf->specidents()->Get(i)->ididx())->name()->c_str();
}

static const int *DisAsmIns(string &s, const int *ip, const int *code, const type_elem_t *typetable,
                            const bytecode::BytecodeFile *bcf)
{
    #define F(N) #N,
    static const char *ilnames[] = { ILNAMES };
    #undef F

    auto li = LookupLine(ip, code, bcf);

    // FIXME: some indication of the filename, maybe with a table index?
    s += "I ";
    s += to_string(int(ip - code));
    s += " \tL ";
    s += to_string(li->line());
    s += " \t";
    s += ilnames[*ip];
    s += " ";

    int opc = *ip++;

    switch(opc)
    {
        case IL_IFOR:
        case IL_VFOR:
        case IL_SFOR:
        case IL_IFORREF:
        case IL_VFORREF:
        case IL_SFORREF:
        case IL_PUSHINT:
        case IL_PUSHFUN:
        case IL_CONT1:
        case IL_JUMP:
        case IL_JUMPFAIL:
        case IL_JUMPFAILR:
        case IL_JUMPFAILN:
        case IL_JUMPNOFAIL:
        case IL_JUMPNOFAILR:
        case IL_JUMPFAILREF:
        case IL_JUMPFAILRREF:
        case IL_JUMPFAILNREF:
        case IL_JUMPNOFAILREF:
        case IL_JUMPNOFAILRREF:
        case IL_LOGREAD:
        case IL_LOGREADREF:
        case IL_ISTYPE:
        case IL_EXIT:
            s += to_string(*ip++);
            break;

        case IL_RETURN:
        {
            auto id = *ip++;
            ip++;  // retvals
            ip++;  // rettype
            s += id >= 0 ? bcf->functions()->Get(id)->name()->c_str() : to_string(id);
            break;
        }

        case IL_CALLV:
        case IL_CALLVCOND:
            s += to_string(*ip++);
            s += " m:";
            s += to_string(*ip++);
            break;

        case IL_CALL:
        case IL_CALLMULTI:
        {
            auto nargs = *ip++;
            auto id = *ip++;
            auto bc = *ip++;
            auto tm = *ip++;
            if (opc == IL_CALLMULTI) ip += nargs;  // arg types.
            s += to_string(nargs);
            s += " ";
            s += bcf->functions()->Get(id)->name()->c_str();
            s += " ";
            s += to_string(bc);
            s += " m:";
            s += to_string(tm);
            break;
        }

        case IL_NEWVEC:
        {
            auto ti = (TypeInfo *)(typetable + *ip++);
            auto nargs = *ip++;
            s += ti->t == V_STRUCT ? bcf->structs()->Get(ti->structidx)->name()->c_str() : "vector";
            s += " ";
            s += to_string(nargs);
            break;
        }

        case IL_BCALL:
        {
            int a = *ip++;
            s += natreg.nfuns[a]->name;
            break;
        }

        case IL_LVALVAR:
            LvalDisAsm(s, ip);
        case IL_PUSHVAR:
        case IL_PUSHVARREF:
            //s += to_string(*ip) + ":";
            s += IdName(bcf, *ip++);
            break;

        case IL_LVALFLD:
        case IL_LVALLOC:
           LvalDisAsm(s, ip);
        case IL_PUSHFLD:
        case IL_PUSHFLDM:
        case IL_PUSHLOC:
            s += to_string(*ip++);
            break;

        case IL_LVALIDXI:
        case IL_LVALIDXV:
            LvalDisAsm(s, ip);
            break;

        case IL_PUSHFLT:
            s += to_string(*(float *)ip);
            ip++;
            break;

        case IL_PUSHSTR:
            s += "\"";
            while(*ip) s += (char)*ip++;
            ip++;
            s += "\"";
            break;

        case IL_FUNSTART:
        {
            int n = *ip++;
            while (n--) { s += IdName(bcf, *ip++); s += " "; }
            n = *ip++; 
            s += "=> ";
            while (n--) { s += IdName(bcf, *ip++); s += " "; }
            n = *ip++;
            if (n) { s += "(log = "; s += to_string(n); s += ")"; }
            break;
        }

        case IL_CORO:
        {
            s += to_string(*ip++);
            ip++;  // typeinfo
            int n = *ip++;
            for (int i = 0; i < n; i++) { s += " v"; s += to_string(*ip++); }
            break;
        }

        case IL_FUNMULTI:
        {
            auto n = *ip++;
            auto nargs = *ip++;
            s += to_string(n);
            s += " ";
            s += to_string(nargs);
            ip += (nargs * 2 + 1) * n;
        }
    }

    return ip;
}

void DisAsm(string &s, const uchar *bytecode_buffer)
{
    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer);
    assert(FLATBUFFERS_LITTLEENDIAN);
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    auto typetable = (const type_elem_t *)bcf->typetable()->Data();  // Same.
    auto len = bcf->bytecode()->Length();

    const int *ip = code;
    while (ip < code + len)
    {
        ip = DisAsmIns(s, ip, code, typetable, bcf);
        s += "\n";
    }
}

}  // namespace lobster
