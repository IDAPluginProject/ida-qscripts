// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

// Pure path / string / file utilities for qscripts. Self-contained (includes its
// own SDK deps) so it can be unit-tested headlessly.
//
// (collect_extlangs moved to libidacpp::expr; the regex_replace-with-callback
// helper moved to libidacpp::text::regex_replace_cb.)

#pragma once

#include <filesystem>
#include <functional>
#include <regex>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4146)
#include <pro.h>
#include <fpro.h>
#include <prodir.h>   // DIRCHAR / SDIRCHAR
#include <diskio.hpp>
#pragma warning(pop)

//-------------------------------------------------------------------------
// Utility function to return a file's last modification timestamp
inline bool get_file_modification_time(
    const char *filename,
    qtime64_t *mtime = nullptr)
{
    qstatbuf stat_buf;
    if (qstat(filename, &stat_buf) != 0)
        return false;

    if (mtime != nullptr)
        *mtime = stat_buf.qst_mtime;
    return true;
}

template <class STRING>
inline bool get_file_modification_time(
    const STRING &filename,
    qtime64_t *mtime = nullptr)
{
    return get_file_modification_time(filename.c_str(), mtime);
}

//-------------------------------------------------------------------------
inline void normalize_path_sep(qstring &path)
{
#ifdef __FAT__
    path.replace("/", SDIRCHAR);
#else
    path.replace("\\", SDIRCHAR);
#endif
}

//-------------------------------------------------------------------------
inline void make_abs_path(qstring& path, const char* base_dir = nullptr, bool normalize = false)
{
    if (qisabspath(path.c_str()))
        return;

    auto old_cwd = std::filesystem::current_path();
    if (base_dir == nullptr)
    {
        path = old_cwd.string().c_str();
    }
    else
    {
        std::filesystem::current_path(base_dir);
        auto abs = std::filesystem::absolute(path.c_str());
        path = abs.string().c_str();
        std::filesystem::current_path(old_cwd);
    }
    if (normalize)
        normalize_path_sep(path);
}

//-------------------------------------------------------------------------
inline bool get_basename_and_ext(
    const char *path,
    char **basename,
    char **ext,
    qstring &wrk_buf)
{
    wrk_buf = path;
    qsplitfile(wrk_buf.begin(), basename, ext);
    if ((*basename = qstrrchr(wrk_buf.begin(), DIRCHAR)) != nullptr)
        return ++(*basename), true;
    else
        return false;
}

//-------------------------------------------------------------------------
inline void get_current_directory(qstring &dir)
{
    dir = std::filesystem::current_path().string().c_str();
}

//-------------------------------------------------------------------------
inline void enumerate_files(
    const std::filesystem::path& path,
    const std::regex& filter,
    std::function<bool(const std::string&)> callback)
{
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.is_regular_file())
        {
            if (!std::regex_match(entry.path().filename().string(), filter))
                continue;
            if (!callback(entry.path().string()))
                break;
        }
    }
}
