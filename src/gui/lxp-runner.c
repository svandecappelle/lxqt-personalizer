/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-runner.h"

#include "lxp-error.h"

#include <string.h>

struct _LxpRunner {
    GSubprocess *process;
    GDataInputStream *out;
    GDataInputStream *err;
    GString *err_text;
    LxpRunnerEvent on_event;
    LxpRunnerDone on_done;
    gpointer user_data;
    int pending;              /* stdout EOF, stderr EOF, process exit */
    gboolean stdin_closed;
};

char *lxp_runner_find_cli(void)
{
    const char *env = g_getenv("LXP_CLI");
    g_autofree char *self = g_file_read_link("/proc/self/exe", NULL);

    if (env && g_file_test(env, G_FILE_TEST_IS_EXECUTABLE))
        return g_strdup(env);
    if (self) {
        g_autofree char *dir = g_path_get_dirname(self);
        g_autofree char *path = g_build_filename(dir, "lxp", NULL);
        if (g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
            return g_steal_pointer(&path);
    }
    return g_find_program_in_path("lxp");
}

static void finish_one(LxpRunner *r)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *warnings = NULL;

    if (--r->pending > 0)
        return;

    warnings = g_strstrip(g_string_free(g_steal_pointer(&r->err_text), FALSE));
    if (!g_subprocess_get_successful(r->process)) {
        const char *msg = g_str_has_prefix(warnings, "lxp: ") ? warnings + 5 : warnings;
        error = g_error_new_literal(LXP_ERROR, LXP_ERROR_FAILED, *msg ? msg : "lxp failed");
    }
    r->on_done(r, error, error ? "" : warnings, r->user_data);

    g_object_unref(r->process);
    g_object_unref(r->out);
    g_object_unref(r->err);
    g_free(r);
}

static void read_out_cb(GObject *source, GAsyncResult *res, gpointer data)
{
    LxpRunner *r = data;
    g_autofree char *line = g_data_input_stream_read_line_finish_utf8(
        G_DATA_INPUT_STREAM(source), res, NULL, NULL);
    char *space;

    if (!line) {
        finish_one(r);
        return;
    }
    space = strchr(line, ' ');
    if (space)
        *space = '\0';
    if (r->on_event)
        r->on_event(r, line, space ? space + 1 : "", r->user_data);
    g_data_input_stream_read_line_async(r->out, G_PRIORITY_DEFAULT, NULL, read_out_cb, r);
}

static void read_err_cb(GObject *source, GAsyncResult *res, gpointer data)
{
    LxpRunner *r = data;
    g_autofree char *line = g_data_input_stream_read_line_finish_utf8(
        G_DATA_INPUT_STREAM(source), res, NULL, NULL);

    if (!line) {
        finish_one(r);
        return;
    }
    g_string_append_printf(r->err_text, "%s\n", line);
    g_data_input_stream_read_line_async(r->err, G_PRIORITY_DEFAULT, NULL, read_err_cb, r);
}

static void wait_cb(GObject *source, GAsyncResult *res, gpointer data)
{
    g_subprocess_wait_finish(G_SUBPROCESS(source), res, NULL);
    finish_one(data);
}

LxpRunner *lxp_runner_start(const char *const *args, LxpRunnerEvent on_event,
                            LxpRunnerDone on_done, gpointer user_data,
                            GError **error)
{
    g_autofree char *cli = lxp_runner_find_cli();
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    g_auto(GStrv) argv = NULL;
    GSubprocess *process;
    LxpRunner *r;

    if (!cli) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                            "The lxp command-line tool was not found");
        return NULL;
    }
    g_strv_builder_add(builder, cli);
    g_strv_builder_addv(builder, (const char **)args);
    argv = g_strv_builder_end(builder);

    process = g_subprocess_newv((const char *const *)argv,
                                G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                G_SUBPROCESS_FLAGS_STDERR_PIPE, error);
    if (!process)
        return NULL;

    r = g_new0(LxpRunner, 1);
    r->process = process;
    r->on_event = on_event;
    r->on_done = on_done;
    r->user_data = user_data;
    r->err_text = g_string_new(NULL);
    r->pending = 3;
    r->out = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    r->err = g_data_input_stream_new(g_subprocess_get_stderr_pipe(process));

    g_data_input_stream_read_line_async(r->out, G_PRIORITY_DEFAULT, NULL, read_out_cb, r);
    g_data_input_stream_read_line_async(r->err, G_PRIORITY_DEFAULT, NULL, read_err_cb, r);
    g_subprocess_wait_async(process, NULL, wait_cb, r);
    return r;
}

void lxp_runner_send(LxpRunner *runner, const char *line)
{
    GOutputStream *in = g_subprocess_get_stdin_pipe(runner->process);
    g_autofree char *data = g_strconcat(line, "\n", NULL);

    if (runner->stdin_closed)
        return;
    runner->stdin_closed = TRUE;
    g_output_stream_write_all(in, data, strlen(data), NULL, NULL, NULL);
    g_output_stream_close(in, NULL, NULL);
}
