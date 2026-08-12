// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

// Pure tokenizer for a single line of a qscripts ".deps" dependency-index file.
// No IDA SDK: classifies a line into a directive kind + value so the effectful
// parsing (filesystem, expansion, recursion) in the plugin can switch on it, and
// so the classification itself can be unit-tested headlessly.

#pragma once

#include <string>

enum class deps_directive_e
{
    comment,            // blank line or a comment ('//', '#', ';') -> ignore
    pkgbase,            // /pkgbase <path>
    notebook_cells_re,  // /notebook.cells_re <regex>
    notebook_activate,  // /notebook.activate exec_main|exec_all|<other=none>
    notebook,           // /notebook <title>
    reload,             // /reload <command>
    triggerfile,        // /triggerfile [/keep] <path>
    script              // any other non-empty line: a (expandable) script path
};

struct deps_line_t
{
    deps_directive_e kind = deps_directive_e::comment;
    std::string      value;               // directive value, or the script path
    bool             trigger_keep = false; // triggerfile: "/keep" was present
};

// Classify one raw line. Faithful to the original inline parser, including its
// prefix-match semantics (a key matches when the line *starts with* it, and the
// value is the text after the key plus one separator character).
inline deps_line_t parse_deps_line(const std::string &raw)
{
    static const char *ws = " \t\r\n";

    auto b = raw.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};   // blank -> comment
    auto e = raw.find_last_not_of(ws);
    const std::string line = raw.substr(b, e - b + 1);

    if (line.compare(0, 2, "//") == 0 || line[0] == '#' || line[0] == ';')
        return {};   // comment

    auto get_value = [&line](const char *key) -> const char *
    {
        const std::string k(key);
        if (line.compare(0, k.size(), k) != 0)
            return nullptr;
        if (line.size() == k.size())
            return "";                      // key alone -> empty value
        return line.c_str() + k.size() + 1; // skip one separator char
    };

    deps_line_t d;
    if (auto v = get_value("/pkgbase"))           { d.kind = deps_directive_e::pkgbase;           d.value = v; return d; }
    if (auto v = get_value("/notebook.cells_re")) { d.kind = deps_directive_e::notebook_cells_re; d.value = v; return d; }
    if (auto v = get_value("/notebook.activate")) { d.kind = deps_directive_e::notebook_activate; d.value = v; return d; }
    if (auto v = get_value("/notebook"))          { d.kind = deps_directive_e::notebook;          d.value = v; return d; }
    if (auto v = get_value("/reload"))            { d.kind = deps_directive_e::reload;            d.value = v; return d; }
    if (auto tf = get_value("/triggerfile"))
    {
        d.kind = deps_directive_e::triggerfile;
        const std::string tfs = tf;
        if (tfs.compare(0, 5, "/keep") == 0)
        {
            d.trigger_keep = true;
            d.value = (tfs.size() == 5) ? "" : tfs.substr(6);
        }
        else
        {
            d.value = tfs;
        }
        return d;
    }

    d.kind  = deps_directive_e::script;
    d.value = line;
    return d;
}
