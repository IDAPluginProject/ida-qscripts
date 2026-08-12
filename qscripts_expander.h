// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

// The qscripts "$...$" string-expansion engine, extracted from the plugin so it
// can be unit-tested headlessly.
//
// Directives:
//   basename          - basename of ctx.script_file
//   env:VAR           - value of environment variable VAR
//   pkgbase           - ctx.pkg_base
//   pkgmodname        - script_file relative to its pkgbase as 'a.b.c' (no ext)
//   pkgparentmodname  - pkgmodname up to the parent module ('a.b')
//   ext               - platform loader-module add-on suffix (e.g. 64.dll, .so)
//
// The pkgbase for pkgmodname is resolved via an injected callback (the plugin
// resolves it from the active script + its dependencies; tests inject a fake).
// The environment lookup is likewise injectable.

#pragma once

#include <functional>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4146)
#include <pro.h>
#include <loader.hpp>   // LOADER_DLL
#pragma warning(pop)

#include <libidacpp/text/text.hpp>
#include "qscripts_utils.h"   // get_basename_and_ext

//-------------------------------------------------------------------------
// Expansion context for a single script file.
struct expand_ctx_t
{
    // input
    qstring script_file;
    bool    main_file = false;

    // working
    qstring base_dir;
    qstring pkg_base;
    qstring reload_cmd;
};

//-------------------------------------------------------------------------
class expander_t
{
public:
    // Resolve the pkgbase to use for a given script file (dependency-aware).
    using pkgbase_resolver_t = std::function<qstring(const char *script_file)>;
    // Look up an environment variable (returns false if unset).
    using env_getter_t = std::function<bool(const char *name, qstring *out)>;

    explicit expander_t(
            pkgbase_resolver_t pkgbase_of,
            env_getter_t get_env = [](const char *name, qstring *out) { return qgetenv(name, out); })
        : m_pkgbase_of(std::move(pkgbase_of)), m_get_env(std::move(get_env))
    {
    }

    // Expand all $...$ directives in `input`.
    qstring expand(const qstring &input, const expand_ctx_t &ctx) const
    {
        std::string out = libidacpp::text::regex_replace_cb(
            std::string(input.c_str()),
            re_expander(),
            [this, &ctx](const std::smatch &m) -> std::string
            {
                qstring match1 = m.str(1).c_str();

                if (strncmp(match1.c_str(), "pkgmodname", 10) == 0)
                {
                    return expand_pkgmodname(ctx);
                }
                else if (strncmp(match1.c_str(), "pkgparentmodname", 16) == 0)
                {
                    std::string pkgmodname = expand_pkgmodname(ctx);
                    size_t pos = pkgmodname.rfind('.');
                    return pos == std::string::npos ? pkgmodname : pkgmodname.substr(0, pos);
                }
                else if (strncmp(match1.c_str(), "ext", 3) == 0)
                {
                    static_assert(LOADER_DLL[0] == '*');
                    return LOADER_DLL + 1;
                }
                else if (strncmp(match1.c_str(), "pkgbase", 7) == 0)
                {
                    return ctx.pkg_base.c_str();
                }
                else if (strncmp(match1.c_str(), "basename", 8) == 0)
                {
                    char *basename, *ext;
                    qstring wrk_str;
                    get_basename_and_ext(ctx.script_file.c_str(), &basename, &ext, wrk_str);
                    return basename;
                }
                else if (strncmp(match1.c_str(), "env:", 4) == 0)
                {
                    qstring env;
                    if (m_get_env(match1.begin() + 4, &env))
                        return env.c_str();
                }
                return m.str(1);
            });
        return out.c_str();
    }

private:
    static const std::regex &re_expander()
    {
        static const std::regex re(R"(\$(.+?)\$)");
        return re;
    }

    std::string expand_pkgmodname(const expand_ctx_t &ctx) const
    {
        qstring pkg_base = m_pkgbase_of(ctx.script_file.c_str());

        // If the script file is under the package base, turn the relative path
        // into a dotted module name (and drop the extension).
        if (strncmp(ctx.script_file.c_str(), pkg_base.c_str(), pkg_base.length()) == 0)
        {
            qstring s = ctx.script_file.c_str() + pkg_base.length() + 1;
            s.replace(SDIRCHAR, ".");
            auto idx = s.rfind('.');
            if (idx != -1)
                s.resize(idx);
            return s.c_str();
        }
        return "";
    }

    pkgbase_resolver_t m_pkgbase_of;
    env_getter_t       m_get_env;
};
