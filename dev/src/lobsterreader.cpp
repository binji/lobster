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

#include "vmdata.h"
#include "natreg.h"

#include "ttypes.h"
#include "lex.h"

using namespace lobster;

struct ValueParser
{
    vector<string> filenames;
    vector<RefObj *> allocated;
    Lex lex;

    ValueParser(char *_src) : lex("string", filenames, _src)
    {
    }

    ~ValueParser()
    {
        for (auto lo : allocated) lo->Dec();
    }

    Value Parse(type_elem_t typeoff)
    {
        Value v = ParseFactor(typeoff);
        Gobble(T_LINEFEED);
        Expect(T_ENDOFFILE);
        return v;
    }

    Value ParseElems(TType end, type_elem_t typeoff, int numelems = -1)  // Vector or struct.
    {
        Gobble(T_LINEFEED);
        vector<Value> elems;
        auto &ti = g_vm->GetTypeInfo(typeoff);

        if (lex.token == end) lex.Next();
        else
        {
            for (;;)
            {
                if ((int)elems.size() == numelems)
                {
                    ParseFactor(TYPE_ELEM_ANY).DECRT();  // Ignore the value.
                }
                else
                {
                    elems.push_back(ParseFactor(ti.t == V_VECTOR ? ti.subt : ti.elems[elems.size()]));
                }
                bool haslf = lex.token == T_LINEFEED;
                if (haslf) lex.Next();
                if (lex.token == end) break;
                if (!haslf) Expect(T_COMMA);
            }
            lex.Next();
        }

        if (numelems >= 0)
        {
            while ((int)elems.size() < numelems)
            {
                switch (ti.elems[elems.size()])
                {
                    case V_INT:     elems.push_back(Value(0)); break;
                    case V_FLOAT:   elems.push_back(Value(0.0f)); break;
                    case V_NIL:     elems.push_back(Value()); break;
                    default:        lex.Error("no default value possible for missing struct elements");
                }
            }
        }

        auto vec = g_vm->NewVector((int)elems.size(), (int)elems.size(), ti);
        vec->Inc();
        allocated.push_back(vec);
        int i = 0;
        for (auto &e : elems) vec->At(i++) = e;
        return Value(vec);
    }

    void ExpectType(ValueType given, ValueType needed)
    {
        if (given != needed)
        {
            lex.Error(string("type ") +
                      BaseTypeName(needed) +
                      " required, " +
                      BaseTypeName(given) +
                      " given");
        }
    }

    Value ParseFactor(type_elem_t typeoff)
    {
        auto &ti = g_vm->GetTypeInfo(typeoff);
        auto vt = ti.t;

        // TODO: also support boxed parsing as V_ANY.
        // means boxing int/float, deducing runtime type for V_VECTOR, and finding the existing struct.

        switch (lex.token)
        {
            case T_INT:
            { 
                ExpectType(V_INT, vt);
                int i = atoi(lex.sattr.c_str());
                lex.Next();
                return Value(i);
            }

            case T_FLOAT:
            {
                ExpectType(V_FLOAT, vt);
                double f = atof(lex.sattr.c_str());
                lex.Next();
                return Value((float)f);
            }

            case T_STR:
            {
                ExpectType(V_STRING, vt);
                string s = lex.sattr;
                lex.Next();
                auto str = g_vm->NewString(s);
                str->Inc();
                allocated.push_back(str);
                return Value(str);
            }

            case T_NIL:
            {
                ExpectType(V_NIL, vt);
                lex.Next();
                return Value();
            }

            case T_MINUS:
            {
                lex.Next(); 
                Value v = ParseFactor(typeoff);
                switch (typeoff)
                {
                    case TYPE_ELEM_INT:   v.setival(v.ival() * -1); break;
                    case TYPE_ELEM_FLOAT: v.setfval(v.fval() * -1); break;
                    default: lex.Error("unary minus: numeric value expected");
                }
                return v;
            }

            case T_LEFTBRACKET:
            {
                ExpectType(V_VECTOR, vt);
                lex.Next();
                return ParseElems(T_RIGHTBRACKET, typeoff);
            }

            case T_IDENT:
            {
                ExpectType(V_STRUCT, vt);
                string sname = lex.sattr;
                lex.Next();
                Expect(T_LEFTCURLY);
                auto name = g_vm->StructName(ti);
                if (name != sname) lex.Error("struct type " + name + " required, " + sname + " given");
                return ParseElems(T_RIGHTCURLY, typeoff, ti.len);
            }

            default:
                lex.Error("illegal start of expression: " + lex.TokStr());
                return Value();
        }
    }

    void Expect(TType t)
    {
        if (lex.token != t)
            lex.Error(lex.TokStr(t) + " expected, found: " + lex.TokStr());

        lex.Next();
    }

    void Gobble(TType t)
    {
        if (lex.token == t) lex.Next();
    }
};

static Value ParseData(type_elem_t typeoff, char *inp)
{
    try
    {
        ValueParser parser(inp);
        g_vm->Push(parser.Parse(typeoff));
        return Value();
    }
    catch (string &s)
    {
        g_vm->Push(Value());
        return Value(g_vm->NewString(s));
    }
}

void AddReaderOps()
{
    STARTDECL(parse_data) (Value &type, Value &ins)
    {
        Value v = ParseData((type_elem_t)type.ival(), ins.sval()->str());
        ins.DECRT();
        return v;
    }
    ENDDECL2(parse_data, "typeid,stringdata", "TS", "A1?S?",
        "parses a string containing a data structure in lobster syntax (what you get if you convert an arbitrary data"
        " structure to a string) back into a data structure. supports int/float/string/vector and structs."
        " structs will be forced to be compatible with their current definitions, i.e. too many elements will be"
        " truncated, missing elements will be set to 0/nil if possible."
        " useful for simple file formats. returns the value and an error string as second return value"
        " (or nil if no error)");
}

AutoRegister __aro("parsedata", AddReaderOps);


