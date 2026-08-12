// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

// The qscripts file-monitor engine, extracted from the plugin so the dependency
// detection/parsing/orchestration and the "what changed -> what to run" tick can
// be unit-tested headlessly (against temp files, with a fake host).
//
// Only a handful of operations actually need IDA (running a script via extlang,
// running a /reload snippet, refreshing the chooser, logging); those are hoisted
// behind `monitor_host_t`. Everything else here is filesystem + state.

#pragma once

#include <map>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "qscripts_utils.h"     // get_file_modification_time (+ pro.h/fpro.h/prodir.h/diskio.hpp)
#include "script.hpp"           // fileinfo_t / script_info_t / active_script_info_t
#include "qscripts_expander.h"  // expander_t, expand_ctx_t
#include "qscripts_deps.h"      // parse_deps_line

//-------------------------------------------------------------------------
// Effectful operations the monitor delegates to its host (the IDA plugin, or a
// test fake). Everything else in the monitor is pure filesystem + state, so the
// whole decision logic can be exercised without IDA.
struct monitor_host_t
{
    virtual ~monitor_host_t() = default;

    // Compile & run a script file (extlang). `with_undo` groups its side effects
    // under IDA's Undo. Returns true on success.
    virtual bool exec_script(script_info_t &si, bool with_undo) = 0;

    // Run a dependency's /reload command (extlang eval_snippet). On failure returns
    // false and sets `err`.
    virtual bool exec_reload(script_info_t &dep, qstring &err) = 0;

    // The active script / its dependency set changed; refresh the chooser view.
    virtual void refresh_view() = 0;

    // Emit a message to the user.
    virtual void log(const char *text) = 0;

    // Is the file monitor currently armed?
    virtual bool monitor_active() const = 0;

    // The active selection was cleared: drop any host-side active-script state and
    // disarm the monitor.
    virtual void on_selected_cleared() = 0;
};

//-------------------------------------------------------------------------
// Owns the active script + its dependency graph, loads/parses the ".deps" index,
// and runs the file-monitor tick. All effects go through `monitor_host_t`, so the
// entire tick/deps logic is testable headlessly against temp files.
class script_monitor_t
{
public:
    explicit script_monitor_t(
        monitor_host_t &host,
        expander_t::env_getter_t get_env =
            [](const char *name, qstring *out) { return qgetenv(name, out); });

    // --- active-script lifecycle ---

    // Activate `script_file`: (re)parse its dependency index and, for a notebook,
    // capture its initial cell files. Safe to call with a pointer into the current
    // active script (the path is copied before the state is cleared).
    void set_selected(const char *script_file);

    // Clear the active script and notify the host (which disarms the monitor).
    void clear_selected();

    bool has_selected() const { return !m_active.file_path.empty(); }

    const active_script_info_t &active() const { return m_active; }
    active_script_info_t       &active()       { return m_active; }

    // Expand $...$ directives (dependency-aware pkgbase). Used by the host to expand
    // a dependency's /reload command before running it.
    qstring expand(const qstring &input, const expand_ctx_t &ctx) const
    {
        return m_expander.expand(input, ctx);
    }

    // The file-monitor tick: inspects the active script + its dependencies, drives
    // the host to reload/run as needed, and returns the next interval (ms).
    int tick(bool with_undo, int interval_ms);

private:
    bool parse_deps(expand_ctx_t &ctx);
    void expand_file_name(qstring &filename, const expand_ctx_t &ctx);
    void populate_notebook_cells();
    bool make_meta_filename(const char *filename, const char *extension, qstring &out, bool local_only = false);
    bool find_deps_file(const char *filename, qstring &out);

    monitor_host_t      &m_host;
    active_script_info_t m_active;
    expander_t           m_expander;
};
