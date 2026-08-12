// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

#include <filesystem>

#include "qscripts_monitor.h"

//-------------------------------------------------------------------------
script_monitor_t::script_monitor_t(
    monitor_host_t &host,
    expander_t::env_getter_t get_env)
    : m_host(host),
      m_expander(
          // pkgbase is resolved live from the active script + its dependencies.
          [this](const char *script_file) -> qstring
          {
              auto dep = m_active.has_dep(script_file);
              return dep == nullptr ? m_active.pkg_base : dep->pkg_base;
          },
          std::move(get_env))
{
}

//-------------------------------------------------------------------------
bool script_monitor_t::make_meta_filename(
    const char *filename,
    const char *extension,
    qstring &out,
    bool local_only)
{
    // Check the .qscripts folder
    char dir[2048];
    if (qdirname(dir, sizeof(dir), filename))
    {
        out.sprnt("%s" SDIRCHAR QSCRIPTS_LOCAL SDIRCHAR "%s.%s", dir, qbasename(filename), extension);
        if (qfileexist(out.c_str()))
            return true;
    }

    if (local_only)
        return false;

    // Check the actual script folder
    out.sprnt("%s.%s", filename, extension);
    return qfileexist(out.c_str());
}

//-------------------------------------------------------------------------
bool script_monitor_t::find_deps_file(
    const char *filename,
    qstring &out)
{
    return     make_meta_filename(filename, "deps", out, true)
            || make_meta_filename(filename, "deps.qscripts", out);
}

//-------------------------------------------------------------------------
void script_monitor_t::expand_file_name(qstring &filename, const expand_ctx_t &ctx)
{
    filename = m_expander.expand(filename, ctx);
    make_abs_path(filename, ctx.base_dir.c_str(), true);
}

//-------------------------------------------------------------------------
bool script_monitor_t::parse_deps(expand_ctx_t &ctx)
{
    // Parse the dependency index file
    qstring dep_file;
    if (!find_deps_file(ctx.script_file.c_str(), dep_file))
        return false;

    FILE *fp = qfopen(dep_file.c_str(), "r");
    if (fp == nullptr)
        return false;

    // Get the dependency file directory
    ctx.base_dir.resize(ctx.script_file.size());
    qdirname(ctx.base_dir.begin(), ctx.base_dir.size(), ctx.script_file.c_str());
    ctx.base_dir.resize(strlen(ctx.base_dir.c_str()));

    // Add the dependency file to the active script
    m_active.add_dep_index(dep_file.c_str());

    // Parse each line
    for (qstring line = dep_file; qgetline(&line, fp) != -1;)
    {
        line.trim2();

        // Classify the line (pure tokenizer). Note: the notebook* directives
        // fall through to script handling when this is NOT the main file, to
        // preserve the original behavior.
        deps_line_t d = parse_deps_line(line.c_str());

        if (d.kind == deps_directive_e::comment)
            continue;

        if (d.kind == deps_directive_e::pkgbase)
        {
            if (ctx.main_file)
            {
                ctx.pkg_base = d.value.c_str();
                expand_file_name(ctx.pkg_base, ctx);
                make_abs_path(ctx.pkg_base, ctx.base_dir.c_str(), true);
            }
            continue;
        }
        else if (d.kind == deps_directive_e::notebook_cells_re)
        {
            if (ctx.main_file)
            {
                m_active.notebook.cells_re = std::regex(d.value);
                continue;
            }
        }
        else if (d.kind == deps_directive_e::notebook_activate)
        {
            if (ctx.main_file)
            {
                int act;
                if (d.value == "exec_main")
                    act = notebook_ctx_t::act_exec_main;
                else if (d.value == "exec_all")
                    act = notebook_ctx_t::act_exec_all;
                else
                    act = notebook_ctx_t::act_exec_none;

                m_active.notebook.activation_action = act;
                continue;
            }
        }
        else if (d.kind == deps_directive_e::notebook)
        {
            if (ctx.main_file)
            {
                m_active.b_is_notebook = true;
                m_active.notebook.title = d.value.c_str();
                continue;
            }
        }
        else if (d.kind == deps_directive_e::reload)
        {
            if (ctx.main_file)
                ctx.reload_cmd = d.value.c_str();
            continue;
        }
        else if (d.kind == deps_directive_e::triggerfile)
        {
            if (d.trigger_keep)
                m_active.b_keep_trigger_file = true;

            if (ctx.main_file)
            {
                m_active.trigger_file.refresh(d.value.c_str());
                expand_file_name(m_active.trigger_file.file_path, ctx);
            }
            continue;
        }

        // From here on, the *line* variable is an expandable string leading to a script file
        ctx.script_file = line;
        expand_file_name(line, ctx);
        normalize_path_sep(line);

        // Skip dependency scripts that (do not|no longer) exist
        script_info_t dep_script;
        if (!get_file_modification_time(line, &dep_script.modified_time))
            continue;

        // Add script
        dep_script.file_path  = line.c_str();
        dep_script.reload_cmd = ctx.reload_cmd;
        dep_script.pkg_base   = ctx.pkg_base;

        m_active.dep_scripts[line.c_str()] = std::move(dep_script);

        expand_ctx_t sub_ctx = ctx;
        sub_ctx.script_file  = line;
        sub_ctx.main_file    = false;
        parse_deps(sub_ctx);
    }
    qfclose(fp);

    return true;
}

//-------------------------------------------------------------------------
void script_monitor_t::populate_notebook_cells()
{
    auto& cell_files = m_active.notebook.cell_files;
    auto current_path = std::filesystem::path(m_active.file_path.c_str()).parent_path();
    m_active.notebook.base_path = current_path.string();

    enumerate_files(
        current_path,
        m_active.notebook.cells_re,
        [&cell_files](const std::string& filename)
        {
            qtime64_t mtime;
            get_file_modification_time(filename, &mtime);
            cell_files[filename] = mtime;
            return true;
        }
    );
}

//-------------------------------------------------------------------------
void script_monitor_t::set_selected(const char *script_file)
{
    // Copy the path first: callers (e.g. the tick's dep-index reparse) may pass a
    // pointer into m_active itself, which clear() below would otherwise invalidate.
    qstring path = script_file;

    // Activate a new script
    m_active.clear();
    m_active.refresh(path.c_str());

    // Recursively parse the dependencies and the index files
    expand_ctx_t main_ctx = { path.c_str(), true };
    parse_deps(main_ctx);

    // If a notebook is selected, let's capture all the cell files
    if (m_active.is_notebook())
        populate_notebook_cells();
}

//-------------------------------------------------------------------------
void script_monitor_t::clear_selected()
{
    m_active.clear();
    m_host.on_selected_cleared();
}

//-------------------------------------------------------------------------
int script_monitor_t::tick(bool with_undo, int interval_ms)
{
    do
    {
        // No active script, do nothing
        if (!m_host.monitor_active() || !has_selected())
            break;

        std::unique_ptr<active_script_info_t> notebook_cell_script;
        active_script_info_t* work_script = &m_active;

        //
        // Handle dependencies first
        //

        // Check if the active script or its dependencies are changed:
        // 1. Dependency file --> repopulate it and execute active script
        // 2. Any dependencies --> reload if needed and //
        // 3. Active script --> execute it again
        auto& dep_scripts = m_active.dep_scripts;

        // Let's check the dependencies index files first
        auto mod_stat = m_active.is_any_dep_index_modified();
        if (mod_stat == filemod_status_e::modified)
        {
            // Force re-parsing of the index file
            dep_scripts.clear();
            set_selected(m_active.file_path.c_str());

            // Let's invalidate all the scripts time stamps so we ensure they are re-interpreted again
            m_active.invalidate_all_scripts();

            // Refresh the UI
            m_host.refresh_view();

            // Just leave and come back fast so we get a chance to re-evaluate everything
            return 1; // (1 ms)
        }
        // Dependency index file is gone
        else if (mod_stat == filemod_status_e::not_found && !dep_scripts.empty())
        {
            // Let's just check the active script
            dep_scripts.clear();
        }

        //
        // Check the dependency scripts
        //
        bool dep_script_changed = false;
        bool brk = false;
        for (auto& kv : dep_scripts)
        {
            auto& dep_script = kv.second;
            if (dep_script.get_modification_status() == filemod_status_e::modified)
            {
                qstring err;
                dep_script_changed = true;
                if (dep_script.has_reload_directive()
                    && !m_host.exec_reload(dep_script, err))
                {
                    brk = true;
                    break;
                }
            }
        }
        if (brk)
            break;

        //
        // Notebook mode
        //
        if (m_active.is_notebook())
        {
            auto& last_active_cell = m_active.notebook.last_active_cell;
            auto& cell_files = m_active.notebook.cell_files;
            auto current_path = std::filesystem::path(m_active.file_path.c_str()).parent_path();
            std::unordered_set<std::string> present_files;

            std::string active_cell;
            enumerate_files(
                current_path,
                m_active.notebook.cells_re,
                [&present_files, &last_active_cell, &active_cell, &cell_files](const std::string& filename)
                {
                    present_files.insert(filename);
                    auto p = cell_files.find(filename);

                    qtime64_t mtime;
                    get_file_modification_time(filename.c_str(), &mtime);

                    // New file?
                    if (p == cell_files.end())
                    {
                        cell_files[filename] = mtime;
                    }
                    // File was modified?
                    else if (p->second != mtime)
                    {
                        last_active_cell = active_cell = filename;
                        p->second = mtime;
                        // Stop enumeration; next interval we pick up the rest
                        return false;
                    }
                    return true;
                }
            );

            // Remove missing files from cell_files
            for (auto it = cell_files.begin(); it != cell_files.end();)
            {
                if (present_files.find(it->first) == present_files.end())
                    it = cell_files.erase(it);
                else
                    ++it;
            }

            // We have to always execute a script when a dependency changes:
            // - If a dependency has changed, but no active cells changedthen attempt to use the last active cell.
            if (dep_script_changed && active_cell.empty())
                active_cell = m_active.notebook.last_active_cell;

            // If no modified cell files, then do nothing
            if (!active_cell.empty())
            {
                // ...use the same metadata as the notebook main script, but just execute the given cell
                notebook_cell_script.reset(new active_script_info_t(m_active));
                work_script = notebook_cell_script.get();
                work_script->file_path = active_cell.c_str();
            }
        }
        //
        // Trigger mode
        //
        // In trigger file mode, just wait for the trigger file to be created
        else if (m_active.trigger_based())
        {
            // The monitor waits until the trigger file is created or modified
            auto trigger_status = m_active.trigger_file.get_modification_status(true);
            if (trigger_status != filemod_status_e::modified)
                break;

            // Delete the trigger file
            if (!m_active.b_keep_trigger_file)
                qunlink(m_active.trigger_file.c_str());

            // Always execute the main script even if it was not changed
            m_active.invalidate();
            // ...and proceed with QScript logic
        }

        // Check the main script
        mod_stat = work_script->get_modification_status();
        if (mod_stat == filemod_status_e::not_found)
        {
            // Script no longer exists
            qstring buf;
            buf.sprnt(
                "QScripts detected that the active script '%s' no longer exists!\n",
                work_script->file_path.c_str());
            m_host.log(buf.c_str());
            clear_selected();
            break;
        }

        // Script or its dependencies changed?
        if (dep_script_changed || mod_stat == filemod_status_e::modified)
            m_host.exec_script(*work_script, with_undo);
    } while (false);
    return interval_ms;
}
