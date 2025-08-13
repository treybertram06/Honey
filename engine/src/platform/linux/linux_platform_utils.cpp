#ifdef HN_PLATFORM_LINUX
#include "hnpch.h"
#include "Honey/utils/platform_utils.h"

#include <cstdlib>
#include <cstdio>
#include <memory>
#include <array>
#include <string>
#include <vector>
#include <cstring>

#include <gtk/gtk.h>

namespace Honey {

    // Run command and capture output
    static std::string exec_read_all(const char* cmd, int* out_status = nullptr) {
        std::array<char, 256> buf{};
        std::string out;
        FILE* pipe = popen(cmd, "r");
        if (!pipe) {
            if (out_status) *out_status = -1;
            return "";
        }
        while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
        int status = pclose(pipe);
        if (out_status) *out_status = status;

        // trim trailing whitespace/newlines
        while (!out.empty() && (out.back()=='\n' || out.back()=='\r' || out.back()==' '))
            out.pop_back();
        return out;
    }

    static bool command_exists(const char* cmd) {
        std::string check = "command -v ";
        check += cmd;
        check += " >/dev/null 2>&1";
        return std::system(check.c_str()) == 0;
    }

    // Parse "Description (*.ext;*.ext2)" -> desc, patterns
    static std::pair<std::string, std::vector<std::string>> parse_filter(const char* filter) {
        std::string desc;
        std::vector<std::string> patterns;

        if (!filter || !*filter)
            return {desc, patterns};

        std::string f = filter;
        auto lp = f.find('(');
        auto rp = f.find(')', lp == std::string::npos ? 0 : lp+1);
        if (lp != std::string::npos && rp != std::string::npos && rp > lp+1) {
            desc = f.substr(0, lp);
            while (!desc.empty() && (desc.back()==' '||desc.back()=='\t')) desc.pop_back();
            std::string pat = f.substr(lp+1, rp-lp-1); // e.g. "*.hns" or "*.hns;*.foo"
            // split by ';'
            size_t start = 0;
            while (start < pat.size()) {
                size_t sep = pat.find(';', start);
                std::string one = pat.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
                if (!one.empty()) patterns.push_back(one);
                if (sep == std::string::npos) break;
                start = sep + 1;
            }
        } else {
            // treat entire string as one pattern
            patterns.push_back(f);
        }
        return {desc, patterns};
    }

    // Build kdialog filter string: "pat1 pat2|Description (pat1;pat2)"
    static std::string make_kdialog_filter(const char* filter) {
        auto [desc, pats] = parse_filter(filter);
        if (pats.empty()) return "";
        std::string pat_join_sp; // space separated for kdialog
        std::string pat_join_sc; // semicolon separated for the label
        for (size_t i=0;i<pats.size();++i) {
            if (i) { pat_join_sp += ' '; pat_join_sc += ';'; }
            pat_join_sp += pats[i];
            pat_join_sc += pats[i];
        }
        std::string label = desc.empty() ? pat_join_sc : (desc + " (" + pat_join_sc + ")");
        return "\"" + pat_join_sp + "|" + label + "\"";
    }

    static std::string try_open_kdialog(const char* filter) {
        if (!command_exists("kdialog")) return "";
        std::string cmd = "kdialog --getopenfilename \"$PWD\"";
        std::string f = make_kdialog_filter(filter);
        if (!f.empty()) { cmd += " "; cmd += f; }
        int status = 0;
        std::string out = exec_read_all(cmd.c_str(), &status);
        if (status == 0 && !out.empty() && out != "canceled") return out;
        return "";
    }

    static std::string try_save_kdialog(const char* filter) {
        if (!command_exists("kdialog")) return "";
        std::string cmd = "kdialog --getsavefilename \"$PWD\"";
        std::string f = make_kdialog_filter(filter);
        if (!f.empty()) { cmd += " "; cmd += f; }
        int status = 0;
        std::string out = exec_read_all(cmd.c_str(), &status);
        if (status == 0 && !out.empty() && out != "canceled") return out;
        return "";
    }

    // GTK fallback (reliable across DEs)
    static std::string run_gtk_file_dialog(GtkFileChooserAction action, const char* title, const char* filter) {
        if (!gtk_init_check(0, nullptr)) return "";

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            title,
            nullptr,
            action,
            "_Cancel", GTK_RESPONSE_CANCEL,
            (action == GTK_FILE_CHOOSER_ACTION_SAVE) ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT,
            nullptr
        );

        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), FALSE);
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE) {
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
            gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);
        }

        // Apply filters
        auto [desc, pats] = parse_filter(filter);
        if (!pats.empty()) {
            GtkFileFilter* gtk_filter = gtk_file_filter_new();
            std::string name = desc;
            if (name.empty()) {
                // join patterns for name
                for (size_t i=0;i<pats.size();++i) {
                    if (i) name += ";";
                    name += pats[i];
                }
                name = "Files (" + name + ")";
            }
            gtk_file_filter_set_name(gtk_filter, name.c_str());
            for (const auto& p : pats) gtk_file_filter_add_pattern(gtk_filter, p.c_str());
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), gtk_filter);

            // Also add "All Files"
            GtkFileFilter* allf = gtk_file_filter_new();
            gtk_file_filter_set_name(allf, "All Files");
            gtk_file_filter_add_pattern(allf, "*");
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), allf);
        }

        std::string result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (filename) {
                result = filename;
                g_free(filename);
            }
        }
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        return result;
    }

    std::string FileDialogs::open_file(const char* filter) {
        if (auto p = try_open_kdialog(filter); !p.empty()) return p;
        return run_gtk_file_dialog(GTK_FILE_CHOOSER_ACTION_OPEN, "Open File", filter);
    }

    std::string FileDialogs::save_file(const char* filter) {
        if (auto p = try_save_kdialog(filter); !p.empty()) return p;
        return run_gtk_file_dialog(GTK_FILE_CHOOSER_ACTION_SAVE, "Save File", filter);
    }
}

#endif